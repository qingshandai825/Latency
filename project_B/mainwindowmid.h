#ifndef MAINWINDOWMID_H
#define MAINWINDOWMID_H
#pragma once
#include <QMainWindow>
#include <QWidget>
#include "mainwindow.h"
#include "forward/forwarderwindow.h"
namespace Ui {
class mainwindowmid;
}

class mainwindowmid : public QWidget
{
    Q_OBJECT

public:
    explicit mainwindowmid(QWidget *parent = nullptr);
    ~mainwindowmid();
private slots:
    void mainwindowLiClicked();
    void forwarderWindowClicked();

private:
    Ui::mainwindowmid *ui;
    ForwarderWindow *forwarderWindow = nullptr;
    MainWindow *mainwindow = nullptr;
};

#endif // MAINWINDOWMID_H


