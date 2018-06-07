
win32 {
    MT_BUILD {
        QMAKE_CXXFLAGS_RELEASE += /MT
    }
    LIBS += \
        -lsetupAPI \
}

linux:LIBS += \
        -ludev \
        -lblkid \

macx {
    QT += xml
    LIBS += \
        -framework IOKit \
        -framework CoreFoundation \
}
