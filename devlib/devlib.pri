HEADERS += \
    $$PWD/Mountpoint.h \
    $$PWD/Partition.h \
    $$PWD/StorageDeviceInfo.h \
    $$PWD/StorageDeviceFile.h \
    $$PWD/StorageDeviceService.h \
    $$PWD/devlib.h

SOURCES += \
    $$PWD/error.cc \
    $$PWD/StorageDeviceService.cpp


include(impl/impl.pri)
include(native/native.pri)
