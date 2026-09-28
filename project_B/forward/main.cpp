#include "forwarderwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ForwarderWindow w;
    w.show();
    return a.exec();
}
