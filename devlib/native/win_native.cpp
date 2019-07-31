#include "native.h"

#include <initguid.h>
#include <tchar.h>
#include <usbiodef.h>
#include <windows.h>
#include <devpkey.h>
#include <cfgmgr32.h>   // for MAX_DEVICE_ID_LEN and CM_Get_Device_ID
#include <SetupAPI.h>
#include <fcntl.h>
#include <io.h>

#include <memory>
#include <stdlib.h>     /* malloc, free, rand */

namespace winutil {
    Q_LOGGING_CATEGORY(winlog, "windows_native");

    constexpr auto win32IOBlockDivider(void) {
        return 512;
    }

    static auto physicalDrivePrefix(void) {
        return QStringLiteral("\\\\.\\PhysicalDrive");
    }

    // make uptr for windows handles
    static auto makeHandleUptr(HANDLE handle) ->
        std::unique_ptr<void, decltype(&::CloseHandle)>
    {
        return {handle, ::CloseHandle};
    }

    // convert rootPath returned by QStrogeInfo to winapi mountpoint path
    static auto toMountpointPath(QString const& volumeRootPath) {
        return QString(volumeRootPath).prepend("\\\\.\\").replace('/', "");
    }

    // get driveNumber from physicalDriveName
    static auto driveNumberFromName(QString const& physicalDriveName) {
        return QString(physicalDriveName)
                .replace(physicalDrivePrefix(), "").toInt();
    }

    // get Physical drive name from drive number
    static auto nameFromDriveNumber(int driveNumber) {
        return QString("\\\\.\\PhysicalDrive%1").arg(driveNumber);
    }

    static auto drivesMountedToMountpoint(QString const& mountpoint)
        -> QVector<int>
    {
        auto volumeHandle = ::CreateFile(mountpoint.toStdWString().data(),
                 GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        if (volumeHandle == INVALID_HANDLE_VALUE) {
            return {};
        }

        auto volHandlePtr = makeHandleUptr(volumeHandle);

        VOLUME_DISK_EXTENTS diskExtents;

        int bytesReturned = 0;
        auto successful = ::DeviceIoControl(
            volHandlePtr.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
            NULL, 0, &diskExtents, sizeof(diskExtents),
            (LPDWORD)&bytesReturned, NULL
        );

        if (!successful) {
            return {};
        }

        auto driveNumbers{QVector<int>()};
        driveNumbers.reserve(diskExtents.NumberOfDiskExtents);

        for (auto i = 0u; i < diskExtents.NumberOfDiskExtents; i++) {
           driveNumbers.push_back(diskExtents.Extents[i].DiskNumber);
        }

        return driveNumbers;
    }


    struct DevId { int vid, pid; };

    struct DevInfo {
        DevId devId;
        QString usbPortPath;
    };

    struct DeviceProperties
    {
        QString instanceId;
        QString containerId;
        QString locationPath;
    };

    using DeviceInterfaceHandler = std::function<void(PSP_DEVICE_INTERFACE_DETAIL_DATA)>;
    using DeviceHandler = std::function<void(DeviceProperties)>;


    static auto extractDevPidVidInfo(QString const& instanceId) -> DevId {
        auto extract = [&instanceId] (QString const& key) {
            return QString(instanceId)
                  .replace(QRegularExpression(".*" + key + "_(.{4}).*"), "0x\\1")
                  .toInt(Q_NULLPTR, 16);
        };

        return {extract("VID"), extract("PID")};
    }


    static auto extractSerialNumber(QString const& instanceId) {
        return QString(instanceId)
                .split("\\")[2]
                .toLower();
    }


    #define SUCCESS 0
    #define MAX_PATH_LENGTH 128


    // Was copied from libusb
    static char *sanitize_path(const char *path)
    {
        const char root_prefix[] = {'\\', '\\', '.', '\\'};
        size_t j, size;
        char *ret_path;
        size_t add_root = 0;

        if (path == NULL)
            return NULL;

        size = strlen(path) + 1;

        // Microsoft indiscriminately uses '\\?\', '\\.\', '##?#" or "##.#" for root prefixes.
        if (!((size > 3) && (((path[0] == '\\') && (path[1] == '\\') && (path[3] == '\\'))
                || ((path[0] == '#') && (path[1] == '#') && (path[3] == '#'))))) {
            add_root = sizeof(root_prefix);
            size += add_root;
        }

        ret_path = (char *)malloc(size);
        if (ret_path == NULL)
            return NULL;

        strcpy(&ret_path[add_root], path);

        // Ensure consistency with root prefix
        memcpy(ret_path, root_prefix, sizeof(root_prefix));

        // Same goes for '\' and '#' after the root prefix. Ensure '#' is used
        for (j = sizeof(root_prefix); j < size; j++) {
            ret_path[j] = (char)toupper((int)ret_path[j]); // Fix case too
            if (ret_path[j] == '\\')
                ret_path[j] = '#';
        }

        return ret_path;
    }

    static int get_interface_details(
            HDEVINFO dev_info, PSP_DEVINFO_DATA dev_info_data, LPCGUID guid, DWORD *_index, char **dev_interface_path)
    {
        SP_DEVICE_INTERFACE_DATA dev_interface_data;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A dev_interface_details;
        DWORD size;

        dev_info_data->cbSize = sizeof(SP_DEVINFO_DATA);
        dev_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        for (;;) {
            if (!SetupDiEnumDeviceInfo(dev_info, *_index, dev_info_data)) {
                if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                    qDebug() << "Could not obtain device info data for:" << _index;
                    //return LIBUSB_ERROR_OTHER;
                    return -1;
                }

                // No more devices
                return 0;
            }

            // Always advance the index for the next iteration
            (*_index)++;

            if (SetupDiEnumDeviceInterfaces(dev_info, dev_info_data, guid, 0, &dev_interface_data))
                break;

            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                qDebug() << "Could not obtain interface data for" << _index << "devInst" << dev_info_data->DevInst;
                //return LIBUSB_ERROR_OTHER;
                return -1;
            }

            // Device does not have an interface matching this GUID, skip
        }

        // Read interface data (dummy + actual) to access the device path
        if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &dev_interface_data, NULL, 0, &size, NULL)) {
            // The dummy call should fail with ERROR_INSUFFICIENT_BUFFER
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                qDebug() << "could not access interface data (dummy) for" << _index << "devInst" << dev_info_data->DevInst;
                //return LIBUSB_ERROR_OTHER;
                return -1;
            }
        } else {
            qDebug() << "program assertion failed - http://msdn.microsoft.com/en-us/library/ms792901.aspx is wrong";
            //return LIBUSB_ERROR_OTHER;
            return -1;
        }

        dev_interface_details = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(size);
        if (dev_interface_details == NULL) {
            qDebug() << "could not allocate interface data for" << _index << "devInst" << dev_info_data->DevInst;
            //return LIBUSB_ERROR_NO_MEM;
            return -2;
        }

        dev_interface_details->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &dev_interface_data,
            dev_interface_details, size, NULL, NULL)) {
            free(dev_interface_details);
            qDebug() << "could not access interface data (actual) for" << _index << "devInst" << dev_info_data->DevInst;
            //return LIBUSB_ERROR_OTHER;
            return -1;
        }

        *dev_interface_path = sanitize_path(dev_interface_details->DevicePath);
        free(dev_interface_details);

        if (*dev_interface_path == NULL) {
            //return LIBUSB_ERROR_NO_MEM;
            qDebug() << "could not allocate interface path for" << _index << "devInst" << dev_info_data->DevInst;
            return -2;
        }

        //return LIBUSB_SUCCESS;
        return 0;
    }

    static std::map<QString, int> initialize_root_busses(int &error_code)
    {
        auto rootHubsBussesMap = std::map<QString, int>{};

        HDEVINFO *dev_info, dev_info_intf;
        SP_DEVINFO_DATA dev_info_data;
        DWORD _index = 0;
        //int r = LIBUSB_SUCCESS;
        int r = 0;
        dev_info_intf = SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (dev_info_intf == INVALID_HANDLE_VALUE) {
            qDebug() << "failed to obtain device info list";
            //return LIBUSB_ERROR_OTHER;
            error_code = -1;
        }
            dev_info = &dev_info_intf;
            for (int busNumber = 1; ; busNumber++) {
                //if (r != LIBUSB_SUCCESS)
                if (r != 0)

                    break;

                if (busNumber == UINT8_MAX) {
                    qDebug() << "program assertion failed - found more than" << UINT8_MAX << "buses, skipping the rest.";
                    break;
                }

                char *dev_interface_path = nullptr;
                r = get_interface_details(*dev_info, &dev_info_data, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER, &_index, &dev_interface_path);
                //if ((r != LIBUSB_SUCCESS) || (dev_interface_path == NULL)) {
                if ((r != 0) || (dev_interface_path == NULL)) {
                    _index = 0;
                    break;
                }
                free(dev_interface_path);

                // Read the Device ID path
                char instanceId[MAX_PATH_LENGTH];
                if (!SetupDiGetDeviceInstanceIdA(*dev_info, &dev_info_data, instanceId, sizeof(instanceId), nullptr)) {
                    qDebug() << "could not read the device instance ID for devInst" << dev_info_data.DevInst << ", skipping";
                    continue;
                }
                rootHubsBussesMap[QString(instanceId)] = busNumber;
                qDebug() << "assigning HCD '" << instanceId << "' bus number" << busNumber;
            }

        SetupDiDestroyDeviceInfoList(dev_info_intf);
        return rootHubsBussesMap;
    }


    static auto extractUSBPorts(QString const& locationPath) {
        QString ports = "";
        QRegExp usbPort("USB\\((\\d+)");
        int pos = 0;
        while ((pos = usbPort.indexIn(locationPath, pos)) != -1) {
            ports += QString(".") + usbPort.cap(1);
            pos += usbPort.matchedLength();
        }
        return ports;
    }


    static auto findBusNumber(QString const& instanceId) {
        int error;
        static auto rootHubsBusses = initialize_root_busses(error);
        qDebug() << "Busses" << rootHubsBusses;

        DEVPROPTYPE ulPropertyType;
        CONFIGRET status;
        HDEVINFO hDevInfo;
        SP_DEVINFO_DATA DeviceInfoData;
        WCHAR szDeviceInstanceID [MAX_DEVICE_ID_LEN];
        WCHAR szDesc[1024];
        WCHAR szBuffer[4096];

        // List all connected USB devices
        hDevInfo = SetupDiGetClassDevs (nullptr, nullptr, NULL,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
            qDebug() << "Unable to get list of connected devices";
            return QString{"1"};
        }

        // Find the ones that are driverless
        for (unsigned i = 0; ; i++)  {
            DeviceInfoData.cbSize = sizeof (DeviceInfoData);
            if (!SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData)) {
                qDebug() << "Devices iteration is complete";
                break;
            }

            DeviceProperties devInfo;

            status = CM_Get_Device_ID(DeviceInfoData.DevInst, szDeviceInstanceID , MAX_PATH, 0);
            if (status != CR_SUCCESS) {
                qDebug() << "Unable to get device 'InstanceID'. Skip device";
                continue;
            }
            if (instanceId != QString::fromWCharArray(szDeviceInstanceID)) {
                continue;
            }
            qDebug() << "Target Instance ID was find";

            DEVINST devDevInst = DeviceInfoData.DevInst;
            DEVINST parentDevInst;
            while (1) {

                if (CM_Get_Parent(&parentDevInst, devDevInst, 0) != CR_SUCCESS)
                    break;
                if (CM_Get_Device_ID(parentDevInst, szDeviceInstanceID , MAX_PATH, 0) != CR_SUCCESS) {
                    qDebug() << "Unable to get device 'InstanceID'. continue";
                    continue;
                }

                devDevInst = parentDevInst;
                auto parentInstanceId = QString::fromWCharArray(szDeviceInstanceID);
                qDebug() << "parentInstanceId" << parentInstanceId;
                if (rootHubsBusses.find(parentInstanceId) != rootHubsBusses.end()) {
                    return QString::number(rootHubsBusses[parentInstanceId]);
                }
            }
        }
        qDebug() << "Return default '1'";
        return QString{"1"};
    }


    static auto getUsbPortPath(DeviceProperties const& deviceProperties) {
        auto ports = extractUSBPorts(deviceProperties.locationPath);

        qDebug() << "PORTS:" << ports;

        QString busNumber = findBusNumber(deviceProperties.instanceId);
        return ports.replace(QRegularExpression("^\\."), busNumber + "-");
    }


    static auto driveNumber(QString const& physicalDriveName) {
        STORAGE_DEVICE_NUMBER deviceNumber = {0};

        auto createMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        auto diskHandle = ::CreateFile(
            physicalDriveName.toStdWString().data(),
            0, createMode, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL
        );

        if (diskHandle == INVALID_HANDLE_VALUE) {
            return -1;
        }

        auto diskHandlePtr = makeHandleUptr(diskHandle);

        int bytesReturned = 0;
        auto successful = ::DeviceIoControl(
            diskHandlePtr.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER,
            NULL, 0, &deviceNumber, sizeof(deviceNumber),
            (LPDWORD)&bytesReturned, NULL
        );

        return !successful ?
            -1 : int(deviceNumber.DeviceNumber);
    }


    static bool foreachDevicesInterface(GUID guid, DeviceInterfaceHandler deviceHandler) {
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        SP_DEVICE_INTERFACE_DATA deviceIntData;
        deviceIntData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        HDEVINFO deviceInfoSet =
        ::SetupDiGetClassDevs(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return false;
        }

        QVector<char> buffer;

        for (int i = 0;
             ::SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &guid, i, &deviceIntData);
             i++)
        {
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData;
            int detailDataSize = 0;

            // get required size of detailData
            bool successful = ::SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceIntData,
                                    NULL, 0, /*out*/PDWORD(&detailDataSize), NULL);

            if (detailDataSize == 0) {
                qCWarning(winlog()) << "forEachDevices: get required size failed.";
                continue;
            }

            if (buffer.capacity() < detailDataSize) {
                buffer.reserve(detailDataSize);
            }

            detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
            ::ZeroMemory(detailData, detailDataSize);
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            // get detailData
            successful = ::SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceIntData,
                                 /*out*/detailData, detailDataSize, NULL, NULL);

            if (!successful) {
                qWarning(winlog()) << "availableDevices: get detailData failed.";
                continue;
            }

            deviceHandler(detailData);
        }

        ::SetupDiDestroyDeviceInfoList(deviceInfoSet);

        return true;
    }


    static bool foreachDevices(LPCTSTR pszEnumerator, DeviceHandler handler) {
        DEVPROPTYPE ulPropertyType;
        CONFIGRET status;
        HDEVINFO hDevInfo;
        SP_DEVINFO_DATA DeviceInfoData;
        WCHAR szDeviceInstanceID [MAX_DEVICE_ID_LEN];
        WCHAR szDesc[1024];
        WCHAR szBuffer[4096];

        // List all connected USB devices
        hDevInfo = SetupDiGetClassDevs (nullptr, pszEnumerator, NULL,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
            qDebug() << "Unable to get list of connected devices";
            return false;
        }

        // Find the ones that are driverless
        for (unsigned i = 0; ; i++)  {
            DeviceInfoData.cbSize = sizeof (DeviceInfoData);
            if (!SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData)) {
                qDebug() << "Devices iteration is complete";
                break;
            }

            DeviceProperties devInfo;

            status = CM_Get_Device_ID(DeviceInfoData.DevInst, szDeviceInstanceID , MAX_PATH, 0);
            if (status != CR_SUCCESS) {
                qDebug() << "Unable to get device 'InstanceID'. Skip device";
                continue;
            }

            devInfo.instanceId = QString::fromWCharArray(szDeviceInstanceID);
            qDebug() << "Device 'InstanceID':" << devInfo.instanceId;

            if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_BusReportedDeviceDesc,
                                                                              &ulPropertyType, (BYTE*)szBuffer, sizeof(szBuffer), nullptr, 0)) {
                if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_ContainerId,
                                                  &ulPropertyType, (BYTE*)szDesc, sizeof(szDesc), nullptr, 0)) {
                    StringFromGUID2((REFGUID)szDesc, szBuffer, sizeof(szBuffer)/sizeof(szBuffer[0]) /*Array size*/);
                    devInfo.containerId = QString::fromWCharArray(szBuffer);
                    qDebug() << "Device 'ContainerId':" << devInfo.containerId;
                } else {
                    qDebug() << "Unable to get device 'ContainerId'";
                }
                if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_LocationPaths,
                                                  &ulPropertyType, (BYTE*)szBuffer, sizeof(szBuffer), nullptr, 0)) {
                    devInfo.locationPath = QString::fromWCharArray(szBuffer);
                    qDebug() << "Device 'LocationPaths':" << devInfo.locationPath;
                } else {
                    qDebug() << "Unable to get device 'LocationPaths'";
                }
            } else {
                qDebug() << "Unable to get device 'BusReportedDeviceDesc'";
            }

            handler(devInfo);
            qDebug();
        }

        return true;
    }


    static auto deviceDiskPath(QString const& instanceId) {
        auto usbDeviceSerialNumber = extractSerialNumber(instanceId);
        auto deviceDiskPath = QString();

        foreachDevicesInterface(GUID_DEVINTERFACE_DISK, [&deviceDiskPath, &usbDeviceSerialNumber]
            (auto detailData) -> void {
                auto path = QString::fromWCharArray(detailData->DevicePath);
                if (!path.contains(usbDeviceSerialNumber)) return;

                deviceDiskPath = std::move(path);
            }
        );

        return deviceDiskPath;
    }


    struct WinHandle : public virtual devlib::native::LockHandle,
                       public virtual devlib::native::io::FileHandle
    {
        HANDLE handle;

        WinHandle(HANDLE in_handle) : handle(in_handle) { }
        virtual ~WinHandle() { ::FlushFileBuffers(handle); ::CloseHandle(handle); }
    };


    static auto makeHandle(HANDLE handle) {
        return std::make_unique<WinHandle>(handle);
    }


    static auto getDevInfo(const QString containerId) -> DevInfo {
        DevInfo devInfo;
        qDebug() << "USB filter iteration: BEGIN";
        foreachDevices(TEXT("USB"),
            [&containerId, &devInfo] (DeviceProperties deviceProperties) -> void {
                if (deviceProperties.containerId == containerId
                        && !deviceProperties.instanceId.contains("MI_")) {
                    devInfo.devId = extractDevPidVidInfo(deviceProperties.instanceId);
                    devInfo.usbPortPath = getUsbPortPath(deviceProperties);
                    qDebug() << "ContainerId matched";
                    qDebug() << "device instanceId:" << deviceProperties.instanceId << ", port_path:" << devInfo.usbPortPath;
                }
            }
        );
        qDebug() << "USB filter iteration: END";
        return devInfo;
    }
}


auto devlib::native::umount(QString const& mntpt)
    -> std::unique_ptr<LockHandle>
{
    auto createMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    auto hMountpoint = ::CreateFile(
        mntpt.toStdWString().data(),
        GENERIC_WRITE, createMode,
        NULL, OPEN_EXISTING, 0, NULL
    );

    if (hMountpoint == INVALID_HANDLE_VALUE) {
        return {};
    }

    DWORD bytesreturned;
    auto successful = ::DeviceIoControl(
        hMountpoint, FSCTL_LOCK_VOLUME,
        NULL, 0, NULL, 0, &bytesreturned, NULL
    );

    if (!successful) {
        ::CloseHandle(hMountpoint);
        qCWarning(winutil::winlog()) << "Can not lock volume: " << mntpt;
        return {};
    }

    DWORD junk;
    successful = ::DeviceIoControl(
        hMountpoint, FSCTL_DISMOUNT_VOLUME,
        NULL, 0, NULL, 0, &junk, NULL
    );

    if (!successful) {
        ::CloseHandle(hMountpoint);
        qCWarning(winutil::winlog()) << "Can not umount volume: " << mntpt;
        return {};
    }

    return winutil::makeHandle(hMountpoint);
}


// Temporary unsupported
bool devlib::native::mount(QString const& dev, QString const& path)
{
    Q_UNUSED(dev); Q_UNUSED(path);
    return false;
}


std::vector<QString> devlib::native::mntptsList(void)
{
    auto mntptsInfo = QStorageInfo::mountedVolumes();
    auto mntpts = std::vector<QString>();
    mntpts.reserve(mntptsInfo.size());

    std::transform(mntptsInfo.cbegin(), mntptsInfo.cend(), mntpts.begin(),
        [] (auto const& info) { return info.rootPath(); }
    );

    return mntpts;
}


std::vector<std::pair<QString, QString>>
    devlib::native::mntptsForPartition(QString const& devFilePath)
{
    auto volumes = QStorageInfo::mountedVolumes();
    auto mountpoints = std::vector<std::pair<QString, QString>>();

    if (devFilePath.contains(winutil::physicalDrivePrefix())) {
        auto driveNumber = winutil::driveNumberFromName(devFilePath);

        for (auto const& vol : volumes) {
            auto mntptPath = winutil::toMountpointPath(vol.rootPath());
            auto driveNums = winutil::drivesMountedToMountpoint(mntptPath);

            if (driveNums.contains(driveNumber)) {
                mountpoints.emplace_back(mntptPath, vol.device());
            }
        }
    } else {
        for (auto const& vol : volumes) {
            if (vol.device() == devFilePath) {
                mountpoints.emplace_back(
                    winutil::toMountpointPath(vol.rootPath()),
                    vol.device()
                );
            }
        }
    }

    return mountpoints;
}


std::vector<std::tuple<int, int, QString, QString>>
    devlib::native::requestUsbDeviceList(void)
{
    auto devicesList = std::vector<std::tuple<int, int, QString, QString>>();

    winutil::foreachDevices(TEXT("USBSTOR"),
        [&devicesList] (winutil::DeviceProperties deviceProperties) -> void {
            QString deviceInstanceId = deviceProperties.instanceId;
            auto driveNum = winutil::driveNumber(
                winutil::deviceDiskPath(deviceInstanceId)
            );
            qDebug() << "device driveNum:" << driveNum;

            if (driveNum == -1) {
                return;
            }

            qDebug() << "Getting device info by 'ContainerId':" << deviceProperties.containerId;
            auto devInfo = winutil::getDevInfo(deviceProperties.containerId);
            auto deviceFilePath = winutil::nameFromDriveNumber(driveNum);

            devicesList.emplace_back(
                devInfo.devId.vid,
                devInfo.devId.pid,
                deviceFilePath,
                devInfo.usbPortPath
            );
        }
    );

    qDebug() << "DevicesList:";
    for (auto& device : devicesList) {
        qDebug() << "vid:" << std::get<0>(device)
                << ", pid:" << std::get<1>(device)
                << ", filePath:" << std::get<2>(device)
                << ", portPath:" << std:: get<3>(device)
                << "\n";
    }

    return devicesList;
}


auto devlib::native::devicePartitions(QString const& deviceName)
    -> std::vector<std::tuple<QString, QString>>
{
    auto mountedVols = QStorageInfo::mountedVolumes();
    auto driveNumber = winutil::driveNumberFromName(deviceName);

    auto vols = std::vector<std::tuple<QString, QString>>();

    for (auto const& vol : mountedVols) {
        auto driveNums = winutil::drivesMountedToMountpoint(
            winutil::toMountpointPath(vol.rootPath())
        );

        if (driveNums.contains(driveNumber)) {
            vols.emplace_back(vol.device(), vol.name());
        }
    }

    return vols;
}


auto devlib::native::io::read(FileHandle* handle, char* data, qint64 sz)
    -> qint64
{
    static const auto Win32_divider
        = winutil::win32IOBlockDivider();

    auto read = 0;
    auto winHandle = dynamic_cast<winutil::WinHandle*>(handle)->handle;

    // On Windows each block of data should be multiples of 512
    // for physical drives
    if (sz % Win32_divider == 0) {
        ::ReadFile(winHandle, (void*)data, (DWORD)sz,
               (LPDWORD)&read, nullptr);
    } else {
        auto neededSize = sz + (Win32_divider - sz % Win32_divider);
        auto tempBuffer = std::make_unique<char[]>(neededSize);

        ::ReadFile(winHandle, (void*)tempBuffer.get(), (DWORD)neededSize,
                   (LPDWORD)&read, nullptr);
        std::memcpy(data, tempBuffer.get(), sz);

        read = read == neededSize ? sz : 0;
    }

    return read;
}


auto devlib::native::io::write(FileHandle* handle, char const* data, qint64 sz)
    -> qint64
{
    static const auto Win32_divider
        = winutil::win32IOBlockDivider();

    auto written = 0LL;
    auto winHandle = dynamic_cast<winutil::WinHandle*>(handle)->handle;


    // On Windows each block of data should be multiples of 512
    // for physical drives

    if (sz % Win32_divider == 0) {
        ::WriteFile(winHandle, (void*)data,
                    (DWORD)sz, (LPDWORD)&written, nullptr);
    } else {
        auto neededSize = sz + (Win32_divider - sz % Win32_divider);
        auto tempBuffer = std::make_unique<char[]>(neededSize);

        std::memset(tempBuffer.get(), 0, neededSize);
        std::memcpy(tempBuffer.get(), data, sz);

        ::WriteFile(winHandle, (void*)tempBuffer.get(),
                    (DWORD)neededSize, (LPDWORD)&written, nullptr);

        written = written == neededSize ? sz : 0;
    }

    return written;
}


auto devlib::native::io::open(char const* filename)
    -> std::unique_ptr<FileHandle>
{
    auto handle = ::CreateFile(QString(filename).toStdWString().data(),
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING,
                               FILE_FLAG_NO_BUFFERING
                               | FILE_FLAG_RANDOM_ACCESS
                               | FILE_FLAG_WRITE_THROUGH
                               , NULL);

    return winutil::makeHandle(handle);
}


bool devlib::native::io::seek(FileHandle* handle, qint64 pos)
{
    auto winHandle = dynamic_cast<winutil::WinHandle*>(handle)->handle;
    auto result = ::SetFilePointer(winHandle, (LONG)pos, nullptr, FILE_BEGIN);
    return result == INVALID_SET_FILE_POINTER;
}


void devlib::native::io::sync(FileHandle* handle)
{ Q_UNUSED(handle); /* temporary stub */ }
