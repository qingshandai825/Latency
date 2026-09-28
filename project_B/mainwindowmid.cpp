#include "mainwindowmid.h"
#include "ui_mainwindowmid.h"
#include "ui_forwarderwindow.h"
#include <QVBoxLayout>    // 垂直布局
#include <QHBoxLayout>    // 水平布局
#include <QSplitter>      // 分割窗口（如果使用了）
#include <QTabWidget>     // 标签页（如果使用了）
mainwindowmid::mainwindowmid(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::mainwindowmid)
{
    ui->setupUi(this);

    // 初始化三个子控件
    forwarderWindow = new ForwarderWindow(this);
    mainwindow = new MainWindow(this);

    // 把它们嵌入 MainWindow.ui 中的占位 QWidget 上
    QVBoxLayout* layout1 = new QVBoxLayout(ui->widget);
    layout1->setContentsMargins(0, 0, 0, 0);
    layout1->addWidget(mainwindow);

    QVBoxLayout* layout2 = new QVBoxLayout(ui->widget2);
    layout2->setContentsMargins(0, 0, 0, 0);
    layout2->addWidget(forwarderWindow);
}

mainwindowmid::~mainwindowmid()
{
    delete ui;
    delete mainwindow;
    delete forwarderWindow;
}
void mainwindowmidLiClicked();
void forwarderWindowClicked();



void mainwindowmid::mainwindowLiClicked()
{
    if (!mainwindow) {
        mainwindow = new MainWindow(this);
    }
    mainwindow->show();
    mainwindow->raise();
    mainwindow->activateWindow();
}

void mainwindowmid::forwarderWindowClicked()
{
    if (!forwarderWindow) {
        forwarderWindow = new ForwarderWindow(this);
    }
    forwarderWindow->show();
    forwarderWindow->raise();
    forwarderWindow->activateWindow();
}
