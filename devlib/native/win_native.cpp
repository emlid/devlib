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

#include <QMap>
#include <QSet>

namespace winutil {
    Q_LOGGING_CATEGORY(winlog, "windows_native");

    constexpr auto UNABLE_TO_GET_DEV_PARENT = -1;
    constexpr auto UNABLE_TO_GET_DEV_ID = -2;

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
        DEVINST handle;
    };

    using DeviceInterfaceHandler = std::function<bool(PSP_DEVICE_INTERFACE_DETAIL_DATA)>;
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


    static auto enumerateRootBuses(void) -> QMap<QString, int>
    {
        auto rootHubsBusesMap = QMap<QString, int>{};

        auto deviceInfoSet = ::SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (deviceInfoSet == INVALID_HANDLE_VALUE)
            return {};

        auto busNumber = 0;
        DWORD index = 0;
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        while (SetupDiEnumDeviceInfo(deviceInfoSet, index, &devInfoData)) {
            index++;

            SP_DEVICE_INTERFACE_DATA devInterfaceData;
            devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
            if (!SetupDiEnumDeviceInterfaces(deviceInfoSet, &devInfoData, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER, 0, &devInterfaceData))
                continue;

            busNumber++;

            char instanceId[MAX_DEVICE_ID_LEN];
            if (!SetupDiGetDeviceInstanceIdA(deviceInfoSet, &devInfoData, instanceId, sizeof(instanceId), nullptr)) {
                continue;
            }
            rootHubsBusesMap.insert(instanceId, busNumber);
        }
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return rootHubsBusesMap;
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


    static auto findBusNumber(DEVINST const& handle, QMap<QString, int> & cachedDeviceBuses) {
        static auto rootHubsBuses = QMap<QString, int>{};
        if (rootHubsBuses.isEmpty()) {
            qDebug(winlog()) << "Enumerating buses...";
            rootHubsBuses = enumerateRootBuses();
        }
        qDebug(winlog()) << "Buses" << rootHubsBuses;

        auto currentDevInst = handle;
        DEVINST parentDevInst;
        WCHAR instanceID [MAX_DEVICE_ID_LEN];

        auto traversedDevices = QSet<QString>{};
        CM_Get_Device_ID(currentDevInst, instanceID , MAX_PATH, 0);
        traversedDevices.insert(QString::fromWCharArray(instanceID));

        int bus;
        for(;;) {
            if (CM_Get_Parent(&parentDevInst, currentDevInst, 0) != CR_SUCCESS) {
                bus = UNABLE_TO_GET_DEV_PARENT;
                break;
            }
            if (CM_Get_Device_ID(parentDevInst, instanceID , MAX_PATH, 0) != CR_SUCCESS) {
                bus = UNABLE_TO_GET_DEV_ID;
                break;
            }

            auto parentInstanceId = QString::fromWCharArray(instanceID);
            qDebug(winlog()) << "parentInstanceId" << parentInstanceId;
            if (rootHubsBuses.contains(parentInstanceId)) {
                bus = rootHubsBuses[parentInstanceId];
                break;
            }
            if (cachedDeviceBuses.contains(parentInstanceId)) {
                bus = cachedDeviceBuses[parentInstanceId];
                break;
            }
            traversedDevices.insert(parentInstanceId);
            currentDevInst = parentDevInst;
        }

        for (auto & devInstanceId : traversedDevices) {
            cachedDeviceBuses[devInstanceId] = bus;
        }
        return QString::number(bus);
    }


    static auto getUsbPortPath(const DEVINST & handle,
                               const QString & locationPath,
                               QMap<QString, int> & cachedDeviceBuses) {
        auto ports = extractUSBPorts(locationPath);

        QString busNumber = findBusNumber(handle, cachedDeviceBuses);
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


    static bool foreachDevicesInterface(GUID guid, DeviceInterfaceHandler deviceInterfaceHandler) {
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

            if (deviceInterfaceHandler(detailData))
                break;
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
        if (hDevInfo == INVALID_HANDLE_VALUE)
            return false;

        // Find the ones that are driverless
        for (unsigned i = 0; ; i++)  {
            DeviceInfoData.cbSize = sizeof (DeviceInfoData);
            if (!SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData))
                break;

            DeviceProperties devInfo;

            status = CM_Get_Device_ID(DeviceInfoData.DevInst, szDeviceInstanceID , MAX_PATH, 0);
            if (status != CR_SUCCESS)
                continue;

            devInfo.handle = DeviceInfoData.DevInst;
            devInfo.instanceId = QString::fromWCharArray(szDeviceInstanceID);

            if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_BusReportedDeviceDesc,
                                                                              &ulPropertyType, (BYTE*)szBuffer, sizeof(szBuffer), nullptr, 0)) {
                if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_ContainerId,
                                                  &ulPropertyType, (BYTE*)szDesc, sizeof(szDesc), nullptr, 0)) {
                    StringFromGUID2((REFGUID)szDesc, szBuffer, sizeof(szBuffer)/sizeof(szBuffer[0]) /*Array size*/);
                    devInfo.containerId = QString::fromWCharArray(szBuffer);
                }
                if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_LocationPaths,
                                                  &ulPropertyType, (BYTE*)szBuffer, sizeof(szBuffer), nullptr, 0)) {
                    devInfo.locationPath = QString::fromWCharArray(szBuffer);
                }
            }

            handler(devInfo);
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
        return true;
    }


    static auto deviceDiskPath(QString const& instanceId) {
        auto usbDeviceSerialNumber = extractSerialNumber(instanceId);
        auto deviceDiskPath = QString();

        foreachDevicesInterface(GUID_DEVINTERFACE_DISK, [&deviceDiskPath, &usbDeviceSerialNumber]
            (auto detailData) -> bool {
                auto path = QString::fromWCharArray(detailData->DevicePath);
                if (!path.contains(usbDeviceSerialNumber)) return false;

                deviceDiskPath = std::move(path);
                return true;
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


    static auto getMapOfUsbDevicesByContainerIds(void) -> QMap<QString, DeviceProperties> {
        auto usbDevicesByContainerIdsMap = QMap<QString, DeviceProperties>{};
        foreachDevices(TEXT("USB"),
            [&usbDevicesByContainerIdsMap] (DeviceProperties deviceProperties) -> void {
                if (!deviceProperties.instanceId.contains("MI_")) {
                    usbDevicesByContainerIdsMap.insert(deviceProperties.containerId,
                                                       deviceProperties);
                }
            }
        );
        return usbDevicesByContainerIdsMap;
    }
}


auto devlib::native::umountPartition(QString const& mntpt)
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


// Temporarily unsupported
bool devlib::native::umountDisk(QString const & devicePath)
{
    Q_UNUSED(devicePath);
    return false;
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
    auto usbDevicesByContainerIdsMap = winutil::getMapOfUsbDevicesByContainerIds();
    auto cachedDevicesBuses = QMap<QString, int>{};

    winutil::foreachDevices(TEXT("USBSTOR"),
        [&devicesList, &usbDevicesByContainerIdsMap, &cachedDevicesBuses]
                            (winutil::DeviceProperties deviceProperties) -> void {
            QString deviceInstanceId = deviceProperties.instanceId;
            auto driveNum = winutil::driveNumber(
                winutil::deviceDiskPath(deviceInstanceId)
            );

            if (driveNum == -1) {
                return;
            }

            auto usbDevProperties = usbDevicesByContainerIdsMap.value(deviceProperties.containerId);
            if (usbDevProperties.instanceId.isEmpty()) {
                qCritical(winutil::winlog()) << "Unable to find USB device using containerId";
                return;
            }

            auto devId = winutil::extractDevPidVidInfo(usbDevProperties.instanceId);
            auto usbPortPath = winutil::getUsbPortPath(usbDevProperties.handle, usbDevProperties.locationPath, cachedDevicesBuses);
            auto deviceFilePath = winutil::nameFromDriveNumber(driveNum);

            devicesList.emplace_back(
                devId.vid,
                devId.pid,
                std::move(deviceFilePath),
                std::move(usbPortPath)
            );
        }
    );

    qDebug(winutil::winlog()) << "Devices list:";
    for (auto& device : devicesList) {
        qDebug(winutil::winlog()) << "vid:" << std::get<0>(device)
                << ", pid:" << std::get<1>(device)
                << ", filePath:" << std::get<2>(device)
                << ", portPath:" << std:: get<3>(device);
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
