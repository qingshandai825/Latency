#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>

class NetworkWorker : public QObject {
    Q_OBJECT
public:
    explicit NetworkWorker(QObject *parent = nullptr);
    ~NetworkWorker();
    void startTest(const QString &ip, int port, int pktSize, int bgMbps, bool useUDP);
    void stopTest();
    void sendProbe();
    void setDelayThreshold(double thresholdMs) { abnormalThreshold_ = thresholdMs; }
    void setAbnormalCountThreshold(int count) { thresholdCount_ = count; }
    void setSimulateRetransmission(bool simulate);

signals:
    void rttMeasured(double rtt, int bgMbps, bool isRetransmit);
    void statusUpdate(const QString &msg);
    void rttAlarm(const QString &alarmMsg);
    void rttRecovered(const QString &recoverMsg);
    void delayStatsUpdated(double rttMs, double forwarderElapsed, double receiverElapsed); // 新增：统计信号

private slots:
    void handleProbeReply();

private:
    bool useUDP_;
    QString ip_;
    int port_;
    int pktSize_;
    int bgMbps_;
    QTcpSocket *tcp_;
    QUdpSocket *udp_;
    QTimer *probeTimer_;
    QByteArray probePayload_;
    qint64 lastProbeNSec_;
    QElapsedTimer timer_;
    QProcess* bgProcess_;
    bool simulateRetransmission_;
    int probeCount_;
    int abnormalThreshold_ = 100;
    int thresholdCount_ = 3;
    int consecutiveAbnormalCount_ = 0;
};
