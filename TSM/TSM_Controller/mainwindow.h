#pragma once
#include <QMainWindow>
#include "TSMmainwindow.h"
#include "sender_gui/mainwindowLi.h"
QT_BEGIN_NAMESPACE
namespace Ui { class mainwindow; }
QT_END_NAMESPACE

class mainwindow : public QMainWindow {
    Q_OBJECT

public:
    explicit mainwindow(QWidget *parent = nullptr);
    ~mainwindow();

private slots:

    void onOpenMainWindowLiClicked();
    void tsm_controllerButtonClicked();

private:
    Ui::mainwindow *ui = nullptr;
    MainWindowLi *mainWindowLi = nullptr; // 注意大小写，改为与 rttwindow.h 一致
    TSMMainWindow *mainwindowTSM = nullptr;
};
