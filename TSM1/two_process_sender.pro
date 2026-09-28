TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    background_sender \
    TSM_Controller

TSM_Controller.depends = background_sender

sender_gui.depends = background_sender
