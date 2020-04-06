#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <tuple>
#include <vector>

#import <CoreFoundation/CoreFoundation.h>
#import <IOKit/usb/IOUSBLib.h>
#import <IOKit/IOBSD.h>

#include <algorithm>

#include <QtXml>
#include <QList>
#include <QStorageInfo>

#include "native.h"


namespace macxutil {
    Q_LOGGING_CATEGORY(macxlog, "macx_native");


    struct MacxFileHandle : public devlib::native::io::FileHandle {
        int fd;
        constexpr MacxFileHandle(int in_fd) : fd(in_fd) {}
        virtual ~MacxFileHandle(void) { ::fsync(fd); ::close(fd); }
    };


    static auto makeHandle(int fd) {
        return std::make_unique<MacxFileHandle>(fd);
    }


    static auto asMacxFileHandle(devlib::native::io::FileHandle* handle) {
        return dynamic_cast<MacxFileHandle*>(handle);
    }


    struct MacxLock : public devlib::native::LockHandle {
        virtual ~MacxLock() = default;
    };


    static auto makeLock() {
        return std::make_unique<MacxLock>();
    }


    static bool extractValueByKey(
        QDomElement const& domElement, QString const& keyName,
        std::function<void(QString const&)> onSuccess
    ) {
        if (domElement.tagName() == "key" && domElement.text() == keyName) {
            auto valueElement = domElement.nextSiblingElement("string");
            onSuccess(valueElement.text());
            return true;
        }

        return false;
    }


    static auto isDiskName(QString const& diskName) {
        return diskName.startsWith("/dev/disk");
    }


    static auto convertToRawDiskName(QString const& diskName) {
        return QString(diskName).replace("/dev/", "/dev/r");
    }


    auto extractArrayWithPartitionsOfDevice(
            QDomElement const& docElement, QString const& deviceName
    ) -> QDomNode
    {
        auto arrays = docElement.elementsByTagName("array");

        for (int i = 0; i < arrays.count(); i++) {
            auto array = arrays.at(i);
            auto parent = array.parentNode();

            if (!parent.isNull() && parent.isElement()) {
                auto dict = parent.toElement();
                if (dict.tagName() != "dict") { continue; }

                auto dictChilds = dict.childNodes();

                for (auto j = 0; j < dictChilds.count(); j++) {
                    auto dictChild = dictChilds.at(j).toElement();

                    if (dictChild.tagName() == "key" && dictChild.text() == "DeviceIdentifier") {
                        auto devnameElem = dictChild.nextSiblingElement("string");
                        if (devnameElem.text() == deviceName) {
                           return array;
                        }
                    }
                }
            }
        }

        return {};
    }


    auto MYCFStringCopyUTF8String(CFStringRef aString)
        -> char*
    {
        if (aString == NULL) {
            return NULL;
        }

        CFIndex length = CFStringGetLength(aString);
        CFIndex maxSize =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
        char *buffer = (char *)malloc(maxSize);
        if (CFStringGetCString(aString, buffer, maxSize,
                             kCFStringEncodingUTF8)) {
            return buffer;
        }
        free(buffer); // If we failed
        return NULL;
    }


    auto convertHexQStringToDecQString(const QString & hexNumber) -> QString
    {
        return QString::number(hexNumber.toInt(nullptr, 16));
    }


    auto extractBusNumberFromLocationId(const QString & locationID) -> QString
    {
        return convertHexQStringToDecQString(locationID.left(2));
    }


    auto extractUsbPortsFromLocationId(const QString & locationID) -> QStringList
    {
        auto usbPortsInHex = QString{locationID}.remove(0,2) // remove leading bus number
                                                .remove("0") // remove trailing zeros
                                                .split("", QString::SkipEmptyParts);
        auto usbPorts = QStringList{};
        std::transform(usbPortsInHex.cbegin(),
                       usbPortsInHex.cend(),
                       std::back_inserter(usbPorts),
                       convertHexQStringToDecQString);
        return usbPorts;
    }


    auto getUsbPortPath(io_service_t usbDeviceRef) -> QString
    {
        io_name_t locationID;
        auto result = ::IORegistryEntryGetLocationInPlane(usbDeviceRef, kIOServicePlane, locationID);
        if (result != KERN_SUCCESS) {
            return {};
        }
        auto busNumber = extractBusNumberFromLocationId(locationID);
        auto usbPorts = extractUsbPortsFromLocationId(locationID);
        return busNumber + "-" + usbPorts.join(".");
    }
}


auto devlib::native::devicePartitions(QString const& devicePath)
    -> std::vector<std::tuple<QString, QString>>
{
    Q_ASSERT(!devicePath.isEmpty());
    auto deviceName = devicePath.split('/').last();

    qCDebug(macxutil::macxlog()) << "Run 'diskutil list -plist'";

    QProcess diskutil;
    diskutil.start("diskutil list -plist", QIODevice::ReadOnly);
    diskutil.waitForFinished();

    if (diskutil.error() != QProcess::ProcessError::UnknownError) {
        qCWarning(macxutil::macxlog()) << "Diskutil failed. "
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
        qCWarning(macxutil::macxlog()) << "Can not create DOM document "
                                          "from diskutil output. "
                                       << "Detailed: "
                                       << "msg: "  << docError.msg
                                       << "line: " << docError.line
                                       << "col: "  << docError.col;
        return {};
    }

    auto docElement = domDocument.documentElement();
    auto partitionsList = macxutil::
            extractArrayWithPartitionsOfDevice(docElement, deviceName);

    if (partitionsList.isNull()) {
        qCWarning(macxutil::macxlog()) << "diskutil: Partitions list is empty";
        return {};
    }

    auto partitions = partitionsList.childNodes();

    for (auto i = 0; i < partitions.count(); i++) {
        auto partitionDict = partitions.at(i).toElement();
        auto partDictChilds = partitionDict.childNodes();

        QString partName, partLabel;

        for (auto j = 0; j < partDictChilds.count(); j++) {
            auto child = partDictChilds.at(j).toElement();

            macxutil::extractValueByKey(child, "DeviceIdentifier",
                [&partName]  (auto const& value) { partName = value; });
            macxutil::extractValueByKey(child, "VolumeName",
                [&partLabel] (auto const& value) { partLabel = value; });
        }

        qCDebug(macxutil::macxlog()) << "Found partition with "
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
        qCCritical(macxutil::macxlog()) << "can not create master port";
        return {};
    }

    auto matchDictionary = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchDictionary) {
        qCCritical(macxutil::macxlog()) << "can not create matching dictionary";
        return {};
    }

    io_iterator_t ioDevsIterator = 0;
    result = IOServiceGetMatchingServices(masterPort, matchDictionary, &ioDevsIterator);
    if (result != KERN_SUCCESS) {
        qCCritical(macxutil::macxlog()) << "can not find any matching services";
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

        auto bsdName = macxutil::MYCFStringCopyUTF8String(bsdNameRef);

        int vid = 0;
        int pid = 0;

        ::CFNumberGetValue(vidRef, kCFNumberSInt32Type, &vid);
        ::CFNumberGetValue(pidRef, kCFNumberSInt32Type, &pid);

        //usbPortPath is not yet supported. Therefore LocationPortPath is None
        devlist.push_back(std::make_tuple(vid, pid, QString("/dev/%1").arg(bsdName), QString("None")));
    }

    return devlist;
}


auto devlib::native::umount(QString const& mntpt)
    -> std::unique_ptr<LockHandle>
{
    auto mntptName = mntpt.toStdString();

    if (::unmount(mntptName.data(), MNT_FORCE) != 0) {
        qCWarning(macxutil::macxlog()) << "can not unmount: " << mntpt;
        return {};
    }
    return macxutil::makeLock();
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
    auto macxHandle = macxutil::asMacxFileHandle(handle);

    if (sz % Macx_divider == 0) {
        return ::read(macxHandle->fd, data, sz);
    }
    auto readed = 0LL;

    auto neededSize = sz + (Macx_divider - sz % Macx_divider);
    auto tempBuffer = std::make_unique<char[]>(neededSize);

    readed = ::read(macxHandle->fd, tempBuffer.get(), neededSize);
    if (readed == -1) {
        qCCritical(macxutil::macxlog()) << "Can not read from file:"
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
    auto macxHandle = macxutil::asMacxFileHandle(handle);

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
        qCWarning(macxutil::macxlog()) << "Can not write to file: "
                                       << ::strerror(errno);
    }

    return written == neededSize ? sz : 0;
}


auto devlib::native::io::open(char const* filename)
    -> std::unique_ptr<FileHandle>
{
    if (!macxutil::isDiskName(filename)) {
        qCWarning(macxutil::macxlog()) << filename << " is not diskname";
        return {};
    }

    auto rawDiskName = macxutil::
        convertToRawDiskName(filename);

    auto fd = ::open(rawDiskName.toStdString().data(), O_RDWR | O_SYNC);
    if (::fcntl(fd, F_GLOBAL_NOCACHE, 1)) {
        qCWarning(macxutil::macxlog()) << "can not disable buffering";
    }

    if (fd < 0) {
        qCWarning(macxutil::macxlog()) << "open(2) :" << strerror(errno);
        return {};
    }

    return macxutil::makeHandle(fd);
}


bool devlib::native::io::seek(FileHandle* handle, qint64 pos)
{
    Q_ASSERT(handle);
    auto macxHandle = macxutil::asMacxFileHandle(handle);
    return ::lseek(macxHandle->fd, pos, SEEK_SET) != -1;
}


void devlib::native::io::sync(FileHandle* handle)
{ Q_UNUSED(handle); /* temporary stub */ }
