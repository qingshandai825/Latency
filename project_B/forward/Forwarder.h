#ifndef FORWARDER_H
#define FORWARDER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QElapsedTimer>

class Forwarder : public QObject
{
    Q_OBJECT
public:
    explicit Forwarder(QObject *parent = nullptr);
    ~Forwarder();
    void start(int senderListenPort, const QString &recvIp, int recvPort);
    void stop();
    bool isReceiverConnected() const;

    // 新增：设置时延阈值接口
    void setElapsedThreshold(int thresholdMs) { elapsedThreshold_ = thresholdMs; }
    void setAbnormalCountThreshold(int count) { abnormalCountThreshold_ = count; }

signals:
    void statusUpdated(const QString &msg);
    void senderPacketCountChanged(int count);
    void forwardPacketCountChanged(int count);
    void receiverConnectionStatusChanged(bool connected);
    // 新增：时延告警信号
    void elapsedAlarm(const QString &alarmMsg);       // 异常告警
    void elapsedRecovered(const QString &recoverMsg); // 恢复正常

private slots:
    void onSenderNewConnection();
    void onSenderReadyRead();
    void onReceiverConnected();
    void onReceiverDisconnected();
    void onReceiverReadyRead();
    void onReceiverError(QAbstractSocket::SocketError socketError);

private:
    QTcpServer *senderServer_;
    QTcpSocket *senderSocket_;
    QTcpSocket *receiverSocket_;
    int senderPacketCount_;
    int forwardPacketCount_;
    QString recvIp_;
    int recvPort_;

    QMap<quint64, qint64> packetTimestamps_;
    quint64 packetSequence_ = 0;
    QElapsedTimer dataTransmitTimer;

    // 新增：时延阈值与计数器（停止测试后不重置）
    int elapsedThreshold_ = 100;         // 默认阈值100ms
    int abnormalCountThreshold_ = 3;     // 默认连续异常次数
    int consecutiveElapsedAbnormal_ = 0; // 连续异常计数器（不随stop重置）

};

#endif // FORWARDER_H
