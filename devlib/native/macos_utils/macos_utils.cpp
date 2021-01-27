#include "macos_utils.h"

namespace  {

    auto convertHexQStringToDecQString(const QString & hexNumber) -> QString
    {
        return QString::number(hexNumber.toInt(nullptr, 16));
    }

}

namespace macos_utils {
    Q_LOGGING_CATEGORY(macxlog, "macx_native");

    auto makeHandle(int fd) -> std::unique_ptr<MacxFileHandle> {
        return std::make_unique<MacxFileHandle>(fd);
    }


    auto asMacxFileHandle(devlib::native::io::FileHandle* handle) -> MacxFileHandle* {
        return dynamic_cast<MacxFileHandle*>(handle);
    }


    auto makeLock() -> std::unique_ptr<MacxLock> {
        return std::make_unique<MacxLock>();
    }


    bool extractValueByKey(
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


    bool isDiskName(QString const& diskName) {
        return diskName.startsWith("/dev/disk");
    }


    auto convertToRawDiskName(QString const& diskName)  -> QString {
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

