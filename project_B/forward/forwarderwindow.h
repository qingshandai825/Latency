#ifndef FORWARDERWINDOW_H
#define FORWARDERWINDOW_H

#include <QWidget>
#include "forwarder.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ForwarderWindow;
}
QT_END_NAMESPACE

class ForwarderWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ForwarderWindow(QWidget *parent = nullptr);
    ~ForwarderWindow();

private slots:
    void onStartClicked();
    void onStopClicked();
    void updateStatus(const QString &msg);
    void updateSenderPacketCount(int count);
    void updateForwardPacketCount(int count);
    void updateReceiverConnectionStatus(bool connected);
    // 新增：告警相关槽函数
    void onElapsedAlarm(const QString &alarmMsg);
    void onElapsedRecovered(const QString &recoverMsg);

private:
    Ui::ForwarderWindow *ui;
    Forwarder *forwarder_;
};

#endif // FORWARDERWINDOW_H
