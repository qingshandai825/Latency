TARGET = tcp_receiver_with_loss_sim
TEMPLATE = app

SOURCES += \
    $$PWD/receiver_main.cpp \
    $$PWD/receiverwindow.cpp

HEADERS += \
    $$PWD/receiverwindow.h

FORMS += \
    $$PWD/receiverwindow.ui
