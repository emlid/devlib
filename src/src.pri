SOURCES += \
        $$PWD/StorageDeviceService.cpp \


HEADERS += \
        $$PWD/devlib.h \
        $$PWD/Mountpoint.h \
        $$PWD/Partition.h \
        $$PWD/StorageDeviceInfo.h \
        $$PWD/StorageDeviceFile.h \
        $$PWD/StorageDeviceService.h \

include(native/native.pri)
include(impl/impl.pri)
