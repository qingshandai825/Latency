SOURCES += \
    $$PWD/Forwarder.cpp \
    $$PWD/forwarderwindow.cpp \
    $$PWD/main.cpp

HEADERS += \
    $$PWD/Forwarder.h \
    $$PWD/forwarderwindow.h

FORMS += \
    $$PWD/forwarderwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
