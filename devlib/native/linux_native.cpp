#include "native.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <libudev.h>
#include <blkid/blkid.h>

#include <memory>
#include <tuple>
#include <cstring>

#include <QtCore>

namespace linutil {
    Q_LOGGING_CATEGORY(linuxlog, "linux_native");

    static void errnoWarning(char const* function, QString const& message, int error) {
        qCWarning(linuxlog()) << '[' << function << "]: "
                              << message << '\n'
                              << QString("\t[linux errno]: ") + std::strerror(error);
    }


    static void warning(char const* function, QString const& message) {
        qCWarning(linuxlog()) << '[' << function << "]: "
                              << message;
    }


    struct LinLock : public devlib::native::LockHandle {
        ~LinLock(void) = default;
    };


    static auto makeLock(void) {
        return std::make_unique<LinLock>();
    }


    struct LinFileHandle : public devlib::native::io::FileHandle
    {
        int fd;
        LinFileHandle(int in_fd) : fd(in_fd) {}
        ~LinFileHandle(void) {
            fsync(fd);
            if (::close(fd) == -1) {
                auto errnoCache = errno;
                linutil::errnoWarning(__PRETTY_FUNCTION__,
                      QString("can not close handle ").append(fd),
                      errnoCache);
            }
        }

    };


    static auto makeFileHandle(int fd) {
        return std::make_unique<LinFileHandle>(fd);
    }


    static auto asLinFileHandle(devlib::native::io::FileHandle* handle) {
        return dynamic_cast<LinFileHandle*>(handle);
    }


    static auto partitionsCount(QString const& deviceName)
    {
        auto asStdString = deviceName.toStdString();
        auto probe = ::blkid_new_probe_from_filename(asStdString.data());

        if (!probe) {
            linutil::warning(__PRETTY_FUNCTION__,
                  QString("Failed to create blkid probe for device: ")
                      .append(deviceName)
                  );
            return -1;
        }

        auto partlist = ::blkid_probe_get_partitions(probe);

        if (!partlist) {
            qCWarning(linuxlog()) << "Device doesn't have any partitions.";
            return 0;
        }

        auto partCount = ::blkid_partlist_numof_partitions(partlist);

        ::blkid_free_probe(probe);
        return partCount;
    }

    static auto extractUsbPortPath(QString const& devicePath) {
        return QString(devicePath)
              //1-1.1.2
              .replace(QRegularExpression(".*/(\\d+\-[\\d\.]+):.*"), "\\1");
    }
}

auto devlib::native::umountPartition(QString const& mntpt)
    -> std::unique_ptr<LockHandle>
{
    if (::umount2(mntpt.toStdString().data(), 0) != 0) {
        auto errnoCache = errno;

        linutil::errnoWarning(__PRETTY_FUNCTION__,
                      QString("can not mount ").append(mntpt),
                      errnoCache);
        return {};
    }

    return linutil::makeLock();
}


// Temporarily unsupported
bool devlib::native::umountDisk(QString const & devicePath)
{
    Q_UNUSED(devicePath);
    return false;
}


bool devlib::native::mount(QString const& dev, QString const& path)
{
    QProcess mount;
    mount.start(QString("mount %1 %2").arg(dev).arg(path));
    mount.waitForFinished();
    return mount.exitCode() == 0;
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



std::vector<std::pair<QString, QString>> devlib::native::mntptsForPartition(QString const& devFilePath) {
    auto mntptsInfo = QStorageInfo::mountedVolumes();
    auto mntpts = std::vector<std::pair<QString, QString>>();

    for (auto const& mntpt : mntptsInfo) {
        if (QString(mntpt.device()).startsWith(devFilePath)) {
            mntpts.emplace_back(mntpt.rootPath(), mntpt.device());
        }
    }

    return mntpts;
}


std::vector<std::tuple<int, int, QString, QString>>
    devlib::native::requestUsbDeviceList(void)
{
    auto storageDeviceList = std::vector<std::tuple<int, int, QString, QString>>();

    std::unique_ptr<udev, decltype(&udev_unref)>
            manager(::udev_new(), &udev_unref);

    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)>
            enumerate(::udev_enumerate_new(manager.get()), &udev_enumerate_unref);

    ::udev_enumerate_add_match_subsystem(enumerate.get(), "block");
    ::udev_enumerate_add_match_property(enumerate.get(), "DEVTYPE", "disk");
    ::udev_enumerate_scan_devices(enumerate.get());

    ::udev_list_entry* deviceList = ::udev_enumerate_get_list_entry(enumerate.get());
    ::udev_list_entry* entry = nullptr;

    udev_list_entry_foreach(entry, deviceList) {
        auto path = ::udev_list_entry_get_name(entry);
        std::unique_ptr<udev_device, decltype(&udev_device_unref)>
                device(::udev_device_new_from_syspath(manager.get(), path), &udev_device_unref);

        if (device == nullptr) {
            continue;
        }

        auto deviceVid = QString(::udev_device_get_property_value(device.get(), "ID_VENDOR_ID"));
        auto devicePid = QString(::udev_device_get_property_value(device.get(), "ID_MODEL_ID"));
        auto usbPortPath = QString(linutil::extractUsbPortPath(path));
        auto diskPath  = QString(::udev_device_get_devnode(device.get()));

        auto base = 16;
        auto storageDevInfo = std::make_tuple(deviceVid.toInt(nullptr, base),
                                              devicePid.toInt(nullptr, base),
                                              diskPath,
                                              usbPortPath);
        storageDeviceList.push_back(storageDevInfo);
    }

    return storageDeviceList;

}




auto devlib::native::devicePartitions(QString const& deviceName)
    -> std::vector<std::tuple<QString, QString>>
{
    auto partitions = std::vector<std::tuple<QString, QString>>();
    auto partsCount = linutil::partitionsCount(deviceName);

    if (partsCount <= 0) {
        return partitions;
    }

    auto valueOf =[] (auto& param, auto& probe, auto buffer) {
        ::blkid_probe_lookup_value(probe, param, &buffer, nullptr);
        return buffer == nullptr ?
            QString("") : QString(buffer);
    };

    for (auto i = 1; i <= partsCount; i++) {
        auto partName = deviceName;
        partName.append(QString::number(i));

        auto probe = ::blkid_new_probe_from_filename(partName.toStdString().data());

        if (!probe) {
            linutil::warning(__PRETTY_FUNCTION__,
                          QString("Failed to create blkid probe for part: ")
                              .append(partName)
                          );
            continue;
        }

        char const* buffer = nullptr;

        ::blkid_do_probe(probe);
        auto partLabel = valueOf("LABEL", probe, buffer);
        auto fstype    = valueOf("TYPE", probe, buffer);
        ::blkid_free_probe(probe);

        partitions.push_back(std::make_tuple(partName, partLabel));
    }

    return partitions;
}


auto devlib::native::io::read(FileHandle* handle, char* data, qint64 sz)
    -> qint64
{
    auto linHandle = linutil::asLinFileHandle(handle);
    return ::read(linHandle->fd, data, sz);
}


auto devlib::native::io::
    write(FileHandle* handle, char const* data, qint64 sz) -> qint64
{
    auto linHandle = linutil::asLinFileHandle(handle);
    return ::write(linHandle->fd, data, sz);
}


auto devlib::native::io::open(char const* filename)
    -> std::unique_ptr<FileHandle>
{
    auto fd = ::open(filename, O_RDWR | O_SYNC);
    if (fd == -1) {
        auto errnoCache = errno;
        linutil::errnoWarning(__PRETTY_FUNCTION__,
                      QString("can not open file ").append(filename),
                      errnoCache);
        return nullptr;
    }

    return linutil::makeFileHandle(fd);
}


// Temporarily unsupported
auto devlib::native::io::authOpen(char const* filename)
    -> std::unique_ptr<FileHandle>
{
    Q_UNUSED(filename);
    return nullptr;
}


auto devlib::native::io::seek(FileHandle* handle, qint64 pos)
   -> bool
{
    Q_ASSERT(handle);
    auto linHandle = linutil::asLinFileHandle(handle);
    return ::lseek(linHandle->fd, pos, SEEK_SET) != -1;
}


void devlib::native::io::sync(FileHandle* handle)
{
    Q_ASSERT(handle);
    auto linHandle = linutil::asLinFileHandle(handle);
    if (::ioctl(linHandle->fd, BLKFLSBUF) != 0) {
        auto errnoCache = errno;
        linutil::errnoWarning(__PRETTY_FUNCTION__,
                      QString("ioctl fails"),
                      errnoCache);
    }
}
