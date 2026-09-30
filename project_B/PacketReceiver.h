#ifndef PACKETRECEIVER_H
#define PACKETRECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QTimer>
#include <QHostAddress>
#include <QElapsedTimer>
#include "high_res_clock.h"
#include "delayinjector.h"


class PacketReceiver : public QObject
{
    Q_OBJECT
public:
    enum Protocol {UDP,TCP};
    explicit PacketReceiver(QObject *parent = nullptr);

    void setAbnormalThreshold(qint64 threshold);
    void processUdpPacket(const QByteArray &packet); // 用于UDP数据包处理
    void setUdpSenderInfo(const QHostAddress &addr, quint16 port); // 设置UDP发送端地址和端口
    void setForwardTarget(const QHostAddress &addr, quint16 port); // 设置转发给C的数据包地址和端口
    void setAbnormalRepeatThreshold(int n);
    void setTimeMs(double timeMs) { m_timeMs = timeMs; }
    // 新增方法，用于设置 DelayInjector
    void setDelayInjector(DelayInjector *injector);

public slots:
    void appendUdpData(const QByteArray &data); // UDP包接收处理
    void appendData(const QByteArray &data);    // TCP包接收处理

signals:
    void delayReceived(double delay, const QString &protocol, qint64 timestamp);
    void delayAbnormal(qint64 delay, const QString &protocol);
    void packetReceived(const QByteArray &packet, quint8 protocol); // 完整TCP包接收信号
    void udpFragmentReceived(const QByteArray &fragment, int total, int index, qint64 timestamp); // UDP分片接收信号
    void logMessage(const QString &msg);

    // ✅ 新增信号：用于每批次耗时通知 UI
    void batchProcessingTime(quint32 batchNumber, double timeMs);
    // 新增信号：通知 processBuffer 完成并传递所有解析出的数据包
    void tcpPacketsProcessed(const QList<QByteArray> &packets);

private:
    void processBuffer();       // TCP缓冲区处理
    void processUdpBuffer();    // UDP缓冲区处理

    // ✅ 新增函数：用于记录/统计批次耗时
    void processBatchInfo(quint32 batchNumber, quint32 totalCount, qint64 localProcessingNs);

    QUdpSocket *udpSocket = nullptr;
    QTcpServer *tcpServer = nullptr;
    QList<QTcpSocket*> tcpClients;
    QByteArray tcpBuffer;

    qint64 abnormalThreshold = 1000;
    QByteArray udpBuffer;
    QMap<qint64, int> udpTotalCount;
    QUdpSocket *udpForwardSocket = nullptr;
    QHostAddress forwardAddr;
    quint16 forwardPort = 0;
    QHash<qint64, qint64> udpFirstFragmentTime;
    int maxConsecutiveAbnormal = 3;
    QMap<QString, int> abnormalCountMap;
    qint64 firstTcpDataReceivedNs = 0;

    // ✅ 新增：记录批次信息
    QMap<quint32, QElapsedTimer> batchTimerMap;
    QMap<quint32, int> batchPacketCountMap;

    quint32 currentBatchNumber = -1;
    quint32 batchPacketCount = 0;
    quint32 batchTotalPacketCount = 0;
    qint64 batchStartTimeNs = 0;
    qint64 batchProcessingNs = 0;
    // 新增：存储MainWindow传递的timeMs
    double m_timeMs = 0.0;
    qint64 appendTimeNs;
    HighResClock m_clock;
    // 新增 DelayInjector 成员
    DelayInjector *delayInjector;

};

#endif // PACKETRECEIVER_H
