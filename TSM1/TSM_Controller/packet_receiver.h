#ifndef PACKET_RECEIVER_H
#define PACKET_RECEIVER_H
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>

class PacketReceiver : public QObject {
    Q_OBJECT
public:
    explicit PacketReceiver(const QHostAddress &listenAddr, quint16 listenPort, QObject *parent = nullptr);

signals:
    void packetReceived(const QByteArray &packet, const QHostAddress &sender, quint16 senderPort);

private slots:
    void handleReadyRead();

private:
    QUdpSocket *m_socket;
};
#endif // PACKET_RECEIVER_H
