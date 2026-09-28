#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QWidget>
#include "ui_receiverwindow.h"
#include <QVBoxLayout>    // 垂直布局
#include <QHBoxLayout>    // 水平布局
#include <QSplitter>      // 分割窗口（如果使用了）
#include <QTabWidget>     // 标签页（如果使用了）

mainwindow::mainwindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::mainwindow)
{
    ui->setupUi(this);

    // 为主窗口添加滚动区域
    QScrollArea *mainScrollArea = new QScrollArea(this);
    QWidget *mainContentWidget = new QWidget(mainScrollArea);
    mainScrollArea->setWidgetResizable(true);
    mainScrollArea->setWidget(mainContentWidget);

    // 主内容布局（存放原本的 widget 和 widget_2）
    QVBoxLayout *mainContentLayout = new QVBoxLayout(mainContentWidget);
    mainContentLayout->setAlignment(Qt::AlignTop);

    // 初始化子控件并添加到主内容布局
    receiverWindow = new ReceiverWindow(this);
    receiverWindowLi = new ReceiverWindowLi(this);

    mainContentLayout->addWidget(receiverWindow);
    mainContentLayout->addWidget(receiverWindowLi);

    // 设置主窗口的中央部件为滚动区域
    setCentralWidget(mainScrollArea);
}

mainwindow::~mainwindow()
{
    delete ui;
    delete receiverWindow;
    delete receiverWindowLi;
}



void mainwindow::onOpenMainWindowLiClicked()
{
    if (!receiverWindow) {
        receiverWindow = new ReceiverWindow(this);
    }
    receiverWindow->show();
    receiverWindow->raise();
    receiverWindow->activateWindow();
}

void mainwindow::tsm_controllerButtonClicked()
{
    if (!receiverWindowLi) {
        receiverWindowLi = new ReceiverWindowLi(this);
    }
    receiverWindowLi->show();
    receiverWindowLi->raise();
    receiverWindowLi->activateWindow();
}
