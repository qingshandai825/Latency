#include <QApplication>
#include "mainwindowLi.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindowLi w;
    w.show();
    return app.exec();
}
