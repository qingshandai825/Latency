QT       += core gui
QT       += core gui network
QT       += charts
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    TSMmainwindow.cpp \
    delayinjector.cpp \
    high_res_clock.cpp \
    main.cpp \
    mainwindow.cpp \
    packet_receiver.cpp \
    packet_scheduler.cpp \
    traffic_generator.cpp \
    tsm_controller.cpp

HEADERS += \
    TSMmainwindow.h \
    delayinjector.h \
    high_res_clock.h \
    mainwindow.h \
    packet_receiver.h \
    packet_scheduler.h \
    traffic_generator.h \
    tsm_controller.h

FORMS += \
    TSMmainwindow.ui \
    mainwindow.ui \
    mainwindow.ui


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
include(sender_gui/sender_gui.pri)
