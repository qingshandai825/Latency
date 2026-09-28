#ifndef TRAFFIC_GENERATOR_H
#define TRAFFIC_GENERATOR_H
#include <QObject>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QVector>
#include "high_res_clock.h"
#include <QFile>

class QFile;

class TrafficGenerator : public QObject {
    Q_OBJECT
public:
    enum PacketType { Unicast, Multicast, Broadcast };
    enum TransportProtocol { UDP, TCP };

    explicit TrafficGenerator(QObject *parent = nullptr);
    HighResClock* getClock();
    bool configureSocket(PacketType type, TransportProtocol protocol, const QHostAddress &address,
                         quint16 port, quint16 sourcePort = 0);
    // 修改：增加优先级参数
    QByteArray generatePacket(quint32 payloadSize, quint8 priority = 0, quint32 totalPacketCount=0, quint32 batchNumber=0);
    // 修改：增加优先级参数
    QByteArray generateFilePacket(QFile &file, quint32 payloadSize, bool &isLastPacket,
                                  bool isFirstPacket = false, const QString &fileName = QString(),
                                  quint64 fileSize = 0, quint8 priority = 0, quint32 totalPacketCount=0, quint32 batchNumber=0);
    bool sendPacket(const QByteArray &packet, const QHostAddress &dest, quint16 port, quint64 delayNs);
    void resetSequence();
    QVector<QPair<qint64, qint64>> getSentPackets();
    void clearSentPackets();
    // 新增：设置反馈端口（从UI传入）
    void setFeedbackPort(quint16 port) { m_feedbackPort = port; }

private:
    QUdpSocket *m_udpSocket;
    QTcpSocket *m_tcpSocket;
    HighResClock m_clock;
    quint64 m_sequence;
    TransportProtocol m_protocol;
    QHostAddress m_multicastGroup;
    quint16 m_multicastPort;
    QVector<QPair<qint64, qint64>> m_sentPackets;
    const int m_expectedFeedbackSize = 16;  // 反馈包长度固定为 16 字节
    // 新增：专门用于接收B反馈的UDP Socket
    QUdpSocket* m_feedbackUdpSocket;
    // 反馈端口（从UI输入）
    quint16 m_feedbackPort = 0;


private slots:
    void onTcpReadyRead();
    void onUdpReadyRead();

signals:
    void feedbackReceivedTCP(const QByteArray &feedbackData);
    void feedbackReceivedUDP(const QByteArray &feedbackData);
};


#endif
