#include <QApplication>
#include "mainwindowmid.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    mainwindowmid w;
    w.show();
    return a.exec();
}
