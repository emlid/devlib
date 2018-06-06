
win32:LIBS += \
        -lsetupAPI \

linux:LIBS += \
        -ludev \
        -lblkid \

macx {
    QT += xml
    LIBS += \
        -framework IOKit \
        -framework CoreFoundation \
}
