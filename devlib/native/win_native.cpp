#include "native.h"

#include <initguid.h>
#include <tchar.h>
#include <usbiodef.h>
#include <windows.h>
#include <SetupAPI.h>
#include <fcntl.h>
#include <io.h>

#include <memory>

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


    struct DevInfo { int vid, pid; };

    using DeviceHandler = std::function<void(PSP_DEVICE_INTERFACE_DETAIL_DATA)>;


    static auto extractDevInfo(QString const& devicePath) -> DevInfo {
        auto extract = [&devicePath] (QString const& key) {
            return QString(devicePath)
                  .replace(QRegularExpression(".*" + key + "_(.{4}).*"), "0x\\1")
                  .toInt(Q_NULLPTR, 16);
        };

        return {extract("vid"), extract("pid")};
    }


    static auto extractSerialNumber(QString const& devicePath) {
        return QString(devicePath)
            .replace(QRegularExpression(".*#(.*)#{.*"), "\\1");
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


    static bool foreachDevices(GUID guid, DeviceHandler deviceHandler) {
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


    static auto deviceDiskPath(QString const& devicePath) {
        auto usbDeviceSerialNumber = extractSerialNumber(devicePath);
        auto deviceDiskPath = QString();

        foreachDevices(GUID_DEVINTERFACE_DISK, [&deviceDiskPath, &usbDeviceSerialNumber]
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

    winutil::foreachDevices(GUID_DEVINTERFACE_USB_DEVICE,
        [&devicesList] (PSP_DEVICE_INTERFACE_DETAIL_DATA detailData) -> void {
            QString devicePath = QString::fromWCharArray(detailData->DevicePath);

            auto devInfo  = winutil::extractDevInfo(devicePath);
            auto driveNum = winutil::driveNumber(
                winutil::deviceDiskPath(devicePath)
            );

            if (driveNum == -1) {
                return;
            }

            auto deviceFilePath = winutil::nameFromDriveNumber(driveNum);
            auto serialNumber = winutil::extractSerialNumber(devicePath);

            devicesList.emplace_back(
                devInfo.vid, devInfo.pid, deviceFilePath, serialNumber
            );
        }
    );

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
