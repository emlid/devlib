
win32:LIBS += \
        -lsetupAPI \
        -lOle32 \

linux:LIBS += \
        -ludev \
        -lblkid \

macx {
    QT += xml
    LIBS += \
        -framework IOKit \
        -framework CoreFoundation \
        -framework Security \

}
