
HEADERS += $$PWD/native.h

win32 {
    SOURCES += \
        $$PWD/win_native.cpp \
}

linux {
    SOURCES += \
        $$PWD/linux_native.cpp \
}

macx {
    HEADERS += \
        $$PWD/macos_utils/macos_utils.h \

    SOURCES  += \
        $$PWD/macosx_native.cpp \
        $$PWD/macos_utils/macos_utils.cpp \
        $$PWD/macos_utils/unmount_disk.cpp \
        $$PWD/macos_utils/auth_open.cpp \
}
