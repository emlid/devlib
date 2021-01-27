#ifndef MACX_UTIL_H
#define MACX_UTIL_H

#include "../native.h"

#include <unistd.h>

#import <IOKit/usb/IOUSBLib.h>
#import <IOKit/IOBSD.h>

#include <QtXml>


namespace macos_utils {
    Q_DECLARE_LOGGING_CATEGORY(macxlog);


    struct MacxFileHandle : public devlib::native::io::FileHandle {
        int fd;
        constexpr MacxFileHandle(int in_fd) : fd(in_fd) {}
        virtual ~MacxFileHandle(void) { ::fsync(fd); ::close(fd); }
    };


    auto makeHandle(int fd) -> std::unique_ptr<MacxFileHandle>;

    auto asMacxFileHandle(devlib::native::io::FileHandle* handle) -> MacxFileHandle*;

    struct MacxLock : public devlib::native::LockHandle {
        virtual ~MacxLock() = default;
    };


    auto makeLock() -> std::unique_ptr<MacxLock>;

    bool extractValueByKey(
        QDomElement const& domElement, QString const& keyName,
        std::function<void(QString const&)> onSuccess
    );

    bool isDiskName(QString const& diskName);

    auto convertToRawDiskName(QString const& diskName) -> QString;

    auto extractArrayWithPartitionsOfDevice(
            QDomElement const& docElement, QString const& deviceName
    ) -> QDomNode;

    auto MYCFStringCopyUTF8String(CFStringRef aString) -> char*;

    auto extractBusNumberFromLocationId(const QString & locationID) -> QString;

    auto extractUsbPortsFromLocationId(const QString & locationID) -> QStringList;

    auto getUsbPortPath(io_service_t usbDeviceRef) -> QString;

}


#endif // MACX_UTIL_H
