#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_mainwindowLi.h"
#include <QVBoxLayout>    // 垂直布局
#include <QHBoxLayout>    // 水平布局
#include <QSplitter>      // 分割窗口（如果使用了）
#include <QTabWidget>     // 标签页（如果使用了）


mainwindow::mainwindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::mainwindow)
{
    ui->setupUi(this);
    // 初始化三个子控件
    mainWindowLi = new MainWindowLi(this);
    mainwindowTSM = new TSMMainWindow(this);

    // 把它们嵌入 MainWindow.ui 中的占位 QWidget 上
    QVBoxLayout* layout1 = new QVBoxLayout(ui->widget);
    layout1->setContentsMargins(0, 0, 0, 0);
    layout1->addWidget(mainwindowTSM);

    QVBoxLayout* layout2 = new QVBoxLayout(ui->widget_2);
    layout2->setContentsMargins(0, 0, 0, 0);
    layout2->addWidget(mainWindowLi);

}

mainwindow::~mainwindow()
{
    delete ui;
    delete mainWindowLi;
    delete mainwindowTSM;
}



void mainwindow::onOpenMainWindowLiClicked()
{
    if (!mainWindowLi) {
        mainWindowLi = new MainWindowLi(this);
    }
    mainWindowLi->show();
    mainWindowLi->raise();
    mainWindowLi->activateWindow();
}

void mainwindow::tsm_controllerButtonClicked()
{
    if (!mainwindowTSM) {
        mainwindowTSM = new TSMMainWindow(this);
    }
    mainwindowTSM->show();
    mainwindowTSM->raise();
    mainwindowTSM->activateWindow();
}
