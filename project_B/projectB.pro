
QT += core gui network
QT += concurrent
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = project
TEMPLATE = app

SOURCES += main.cpp               PacketReceiver.cpp \
    delayinjector.cpp \
    high_res_clock.cpp \
    mainwindow.cpp \
    mainwindowmid.cpp

HEADERS +=     PacketReceiver.h \
    delayinjector.h \
    high_res_clock.h \
    mainwindow.h \
    mainwindowmid.h

FORMS += \
    mainwindow.ui \
    mainwindowmid.ui

include(forward/Forwarder.pri)
