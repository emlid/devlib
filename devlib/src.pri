SOURCES += \
        $$PWD/StorageDeviceService.cpp \


HEADERS += \
        $$PWD/devlib.h \
        $$PWD/Mountpoint.h \
        $$PWD/Partition.h \
        $$PWD/StorageDeviceInfo.h \
        $$PWD/StorageDeviceFile.h \
        $$PWD/StorageDeviceService.h \


ENABLE_HEADERS_COPY {
    INCLUDE_DIR = "$$OUT_PWD/include"
    mkdir.target = $${INCLUDE_DIR}
    mkdir.commands = $$sprintf($${QMAKE_MKDIR_CMD}, $${INCLUDE_DIR})

    QMAKE_EXTRA_TARGETS += mkdir

    for (header, HEADERS): {
        HEADER_BASENAME = $$basename(header)
        OUT_HEADER_NAME = "$${INCLUDE_DIR}/$${HEADER_BASENAME}"
        TARGET_NAME = $$replace(HEADER_BASENAME,".h","")

        $${TARGET_NAME}.target = $${OUT_HEADER_NAME}
        $${TARGET_NAME}.commands = $${QMAKE_COPY} $$quote($$header) $$quote($$OUT_HEADER_NAME)
        $${TARGET_NAME}.depends = mkdir

        QMAKE_EXTRA_TARGETS += $${TARGET_NAME}
        POST_TARGETDEPS += $${OUT_HEADER_NAME}
    }
}


include(native/native.pri)
include(impl/impl.pri)
