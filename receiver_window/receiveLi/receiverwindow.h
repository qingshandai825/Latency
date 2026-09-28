#ifndef RECEIVERWINDOWLI_H
#define RECEIVERWINDOWLI_H

#include <QMainWindow>
#include <QTcpServer>
#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui { class ReceiverWindowLi; }
QT_END_NAMESPACE

class ReceiverWindowLi : public QMainWindow {
    Q_OBJECT

public:
    ReceiverWindowLi(QWidget *parent = nullptr);
    ~ReceiverWindowLi();

private slots:
    void onStartClicked();
    void onNewConnection();
    void onDataReceived();

private:
    Ui::ReceiverWindowLi *ui;
    QTcpServer *server_;
    QTcpSocket *client_;
    int lossRate_;  // 丢包率

    // 新增：时延阈值与计数器
    int elapsedThreshold_ = 100;         // 默认阈值100ms（可通过UI设置）
    int abnormalCountThreshold_ = 3;     // 默认连续异常次数
    int consecutiveElapsedAbnormal_ = 0; // 连续异常计数器
};
#endif // RECEIVERWINDOWLI_H
