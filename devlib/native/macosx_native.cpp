#include <sys/mount.h>

#import <CoreFoundation/CoreFoundation.h>

#include "native.h"
#include "macos_utils/macos_utils.h"


auto devlib::native::devicePartitions(QString const& devicePath)
    -> std::vector<std::tuple<QString, QString>>
{
    Q_ASSERT(!devicePath.isEmpty());
    auto deviceName = devicePath.split('/').last();

    qCDebug(macos_utils::macxlog()) << "Run 'diskutil list -plist'";

    QProcess diskutil;
    diskutil.start("diskutil list -plist", QIODevice::ReadOnly);
    diskutil.waitForFinished();

    if (diskutil.error() != QProcess::ProcessError::UnknownError) {
        qCWarning(macos_utils::macxlog()) << "Diskutil failed. "
                                          << diskutil.errorString()
                                          << diskutil.readAllStandardError();
    }

    auto devicePartitionnsList = std::vector<
        std::tuple<QString, QString>
    >();

    QDomDocument domDocument;

    struct {
        QString msg;
        int line, col;
    } docError{"", 0, 0};

    if (!domDocument.setContent(&diskutil, &docError.msg, &docError.line, &docError.col) ) {
        qCWarning(macos_utils::macxlog()) << "Can not create DOM document "
                                          "from diskutil output. "
                                          << "Detailed: "
                                          << "msg: "  << docError.msg
                                          << "line: " << docError.line
                                          << "col: "  << docError.col;
        return {};
    }

    auto docElement = domDocument.documentElement();
    auto partitionsList = macos_utils::
            extractArrayWithPartitionsOfDevice(docElement, deviceName);

    if (partitionsList.isNull()) {
        qCWarning(macos_utils::macxlog()) << "diskutil: Partitions list is empty";
        return {};
    }

    auto partitions = partitionsList.childNodes();

    for (auto i = 0; i < partitions.count(); i++) {
        auto partitionDict = partitions.at(i).toElement();
        auto partDictChilds = partitionDict.childNodes();

        QString partName, partLabel;

        for (auto j = 0; j < partDictChilds.count(); j++) {
            auto child = partDictChilds.at(j).toElement();

            macos_utils::extractValueByKey(child, "DeviceIdentifier",
                [&partName]  (auto const& value) { partName = value; });
            macos_utils::extractValueByKey(child, "VolumeName",
                [&partLabel] (auto const& value) { partLabel = value; });
        }

        qCDebug(macos_utils::macxlog()) << "Found partition with "
                                        << "Name: " << partName
                                        << "Label: " << partLabel;

        devicePartitionnsList.push_back(
            std::make_tuple(partName.prepend("/dev/"), partLabel)
        );
    }

    return devicePartitionnsList;
}


auto devlib::native::requestUsbDeviceList(void)
    -> std::vector<std::tuple<int, int, QString, QString>>
{
    std::vector<std::tuple<int, int, QString, QString>> devlist;

    mach_port_t masterPort;
    auto result = ::IOMasterPort(MACH_PORT_NULL, &masterPort);
    if (result != KERN_SUCCESS) {
        qCCritical(macos_utils::macxlog()) << "can not create master port";
        return {};
    }

    auto matchDictionary = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchDictionary) {
        qCCritical(macos_utils::macxlog()) << "can not create matching dictionary";
        return {};
    }

    io_iterator_t ioDevsIterator = 0;
    result = IOServiceGetMatchingServices(masterPort, matchDictionary, &ioDevsIterator);
    if (result != KERN_SUCCESS) {
        qCCritical(macos_utils::macxlog()) << "can not find any matching services";
        return {};
    }

    io_service_t usbDeviceRef;
    while((usbDeviceRef = IOIteratorNext(ioDevsIterator))) {
        auto bsdNameRef = (CFStringRef) ::IORegistryEntrySearchCFProperty(
                        usbDeviceRef, kIOServicePlane,
                        CFSTR(kIOBSDNameKey), kCFAllocatorDefault,
                        kIORegistryIterateRecursively);

        if (!bsdNameRef) {
            continue;
        }

        auto vidRef =  (CFNumberRef) ::IORegistryEntrySearchCFProperty(
                        usbDeviceRef, kIOServicePlane,
                        CFSTR("idVendor"), nullptr,
                        kIORegistryIterateRecursively );

        auto pidRef =  (CFNumberRef) ::IORegistryEntrySearchCFProperty(
                        usbDeviceRef, kIOServicePlane,
                        CFSTR("idProduct"), nullptr,
                        kIORegistryIterateRecursively );

        auto bsdName = macos_utils::MYCFStringCopyUTF8String(bsdNameRef);

        int vid = 0;
        int pid = 0;

        ::CFNumberGetValue(vidRef, kCFNumberSInt32Type, &vid);
        ::CFNumberGetValue(pidRef, kCFNumberSInt32Type, &pid);

        auto usbPortPath = macos_utils::getUsbPortPath(usbDeviceRef);
        if (usbPortPath.isEmpty()) {
            qCCritical(macos_utils::macxlog()) << "Unable to get USB port path";
        }

        devlist.push_back(std::make_tuple(vid, pid, QString("/dev/%1").arg(bsdName), usbPortPath));
    }

    return devlist;
}


auto devlib::native::umountPartition(QString const& mntpt)
    -> std::unique_ptr<LockHandle>
{
    auto mntptName = mntpt.toStdString();

    if (::unmount(mntptName.data(), MNT_FORCE) != 0) {
        qCWarning(macos_utils::macxlog()) << "can not unmount: " << mntpt;
        return {};
    }
    return macos_utils::makeLock();
}


bool devlib::native::umountDisk(QString const & devicePath)
{
    auto unmountResult = macos_utils::unmountDiskWithRunLoop(devicePath.toStdString().data());
    return unmountResult == macos_utils::UnmountResult::Success;
}


bool devlib::native::mount(const QString &dev, const QString &path)
{
    QProcess mount;
    mount.start(QString("mount -t msdos %1 %2").arg(dev).arg(path));
    mount.waitForFinished();
    return mount.exitCode() == 0;
}


auto devlib::native::mntptsForPartition(QString const& devFilePath)
    -> std::vector<std::pair<QString, QString>>
{
    auto mntptsInfo = QStorageInfo::mountedVolumes();
    auto mntpts = std::vector<std::pair<QString, QString>>();

    for (auto const& mntpt : mntptsInfo) {
        if (QString(mntpt.device()).startsWith(devFilePath)) {
            mntpts.emplace_back(mntpt.rootPath(), mntpt.device());
        }
    }

    return mntpts;
}


auto devlib::native::mntptsList(void)
    -> std::vector<QString>
{
    auto mntptsInfo = QStorageInfo::mountedVolumes();
    auto mntpts = std::vector<QString>(mntptsInfo.size());

    std::transform(mntptsInfo.cbegin(), mntptsInfo.cend(), mntpts.begin(),
        [] (auto const& info) { return info.rootPath(); }
    );

    return mntpts;
}


auto devlib::native::io::read(FileHandle* handle, char *data, qint64 sz)
    -> qint64
{
    static auto const Macx_divider = 512;

    Q_ASSERT(handle);
    auto macxHandle = macos_utils::asMacxFileHandle(handle);

    if (sz % Macx_divider == 0) {
        return ::read(macxHandle->fd, data, sz);
    }
    auto readed = 0LL;

    auto neededSize = sz + (Macx_divider - sz % Macx_divider);
    auto tempBuffer = std::make_unique<char[]>(neededSize);

    readed = ::read(macxHandle->fd, tempBuffer.get(), neededSize);
    if (readed == -1) {
        qCCritical(macos_utils::macxlog()) << "Can not read from file:"
                                           << ::strerror(errno);
    } else {
        std::memcpy(data, tempBuffer.get(), sz);
    }


    return readed == neededSize ? sz : 0;
}


auto devlib::native::io::write(FileHandle* handle, char const* data, qint64 sz)
    -> qint64
{
    Q_ASSERT(handle);
    static auto const Macx_divider = 512;
    auto macxHandle = macos_utils::asMacxFileHandle(handle);

    if (sz % Macx_divider == 0) {
        return ::write(macxHandle->fd, data, sz);
    }
    auto written = 0LL;

    auto neededSize = sz + (Macx_divider - sz % Macx_divider);
    auto tempBuffer = std::make_unique<char[]>(neededSize);

    std::memset(tempBuffer.get(), 0, neededSize);
    std::memcpy(tempBuffer.get(), data, sz);

    written = ::write(macxHandle->fd, tempBuffer.get(), neededSize);

    if (written == -1) {
        qCWarning(macos_utils::macxlog()) << "Can not write to file: "
                                          << ::strerror(errno);
    }

    return written == neededSize ? sz : 0;
}


auto devlib::native::io::open(char const* filename)
    -> std::unique_ptr<FileHandle>
{
    if (!macos_utils::isDiskName(filename)) {
        qCWarning(macos_utils::macxlog()) << filename << " is not diskname";
        return {};
    }

    auto rawDiskName = macos_utils::
        convertToRawDiskName(filename);

    auto fd = ::open(rawDiskName.toStdString().data(), O_RDWR | O_SYNC);
    if (::fcntl(fd, F_GLOBAL_NOCACHE, 1)) {
        qCWarning(macos_utils::macxlog()) << "can not disable buffering";
    }

    if (fd < 0) {
        qCWarning(macos_utils::macxlog()) << "open(2) :" << strerror(errno);
        return {};
    }

    return macos_utils::makeHandle(fd);
}


auto devlib::native::io::authOpen(char const* filename)
    -> std::unique_ptr<FileHandle>
{
    if (!macos_utils::isDiskName(filename)) {
         qCWarning(macos_utils::macxlog()) << filename << " is not diskname";
         return {};
     }

     auto rawDiskName = macos_utils::convertToRawDiskName(filename);

     auto fd = macos_utils::authOpenStorageDevice(rawDiskName.toUtf8());

     if (fd < 0) {
         qCWarning(macos_utils::macxlog()) << "Unable to open file with authentication";
         return {};
     }

     return macos_utils::makeHandle(fd);
}


bool devlib::native::io::seek(FileHandle* handle, qint64 pos)
{
    Q_ASSERT(handle);
    auto macxHandle = macos_utils::asMacxFileHandle(handle);
    return ::lseek(macxHandle->fd, pos, SEEK_SET) != -1;
}


void devlib::native::io::sync(FileHandle* handle)
{ Q_UNUSED(handle); /* temporary stub */ }
