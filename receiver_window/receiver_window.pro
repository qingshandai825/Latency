QT       += core gui
QT       += core gui network
QT         += core gui network charts
QT       += charts
QT += core gui widgets charts
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    high_res_clock.cpp \
    main.cpp \
    mainwindow.cpp \
    receiver_window.cpp

HEADERS += \
    high_res_clock.h \
    mainwindow.h \
    receiver_window.h

FORMS += \
    mainwindow.ui \
    receiver_window.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
include(receiveLi/tcp_receiver_with_loss_sim.pri)
