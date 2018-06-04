
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
    SOURCES  += \
        $$PWD/macosx_native.cpp \
}
