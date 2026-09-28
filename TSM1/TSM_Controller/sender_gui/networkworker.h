#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QByteArray>

class NetworkWorker : public QObject {
    Q_OBJECT
public:
    explicit NetworkWorker(QObject *parent = nullptr);
    ~NetworkWorker();

    // 启停与探测
    void startTest(const QString &ip, int port, int pktSize, int bgMbps, bool useUDP);
    void stopTest();
    void sendProbe();  // 主线程通过 invoke 调用

    // 阈值配置
    void setDelayThreshold(int thresholdMs)         { abnormalThreshold_   = thresholdMs; }
    void setAbnormalCountThreshold(int count)       { thresholdCount_      = count; }

    // 勾选“模拟重传”：每第3次探测将 RTT 人为 +50ms 并标记 isRetransmit=true
    void setSimulateRetransmission(bool on) {
        simulateRetransmission_ = on;
        probeCount_ = 0;  // 勾选/取消时从头计数
    }

signals:
    // 统一传毫秒给 UI
    void rttMeasured(double rttMs, int bgMbps, bool isRetransmit);
    void statusUpdate(const QString &msg);
    void rttAlarm(const QString &alarmMsg);
    void rttRecovered(const QString &recoverMsg);
    // A/B/C 段统计（ms）
    void delayStatsUpdated(double aMs, double bMs, double cMs);

private slots:
    void onReadyRead();      // UDP/TCP 共用的 readyRead 槽
    void onProbeTick();      // 定时发送探测

private:
    // 内部工具
    void processRttAndNotify(double aMs, double bMs, double cMs);

    // 配置/状态
    bool        useUDP_  = true;
    QString     ip_;
    int         port_    = 0;
    int         pktSize_ = 0;
    int         bgMbps_  = 0;

    // I/O
    QTcpSocket *tcp_        = nullptr;
    QUdpSocket *udp_        = nullptr;
    QProcess   *bgProcess_  = nullptr;

    // 探测
    QTimer        *probeTimer_    = nullptr; // 每秒探测一次
    QElapsedTimer  rttClock_;                // 发送瞬间 restart()
    QByteArray     probePayload_;            // 记录本次探测包头（时间戳前8字节）
    int            probeCount_     = 0;      // 第几次探测（用于“每3次模拟一次”）

    // 告警
    int abnormalThreshold_         = 100;    // ms
    int thresholdCount_            = 3;
    int consecutiveAbnormalCount_  = 0;

    // 行为模拟
    bool simulateRetransmission_   = false;
};

