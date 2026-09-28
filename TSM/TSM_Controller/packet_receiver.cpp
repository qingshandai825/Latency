#include "packet_receiver.h"
#include <QDebug>

PacketReceiver::PacketReceiver(const QHostAddress &listenAddr, quint16 listenPort, QObject *parent)
    : QObject(parent), m_socket(new QUdpSocket(this)) {

    // if (!m_socket->bind(listenAddr, listenPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
    //     qWarning() << "接收端绑定失败:" << listenAddr.toString() << ":" << listenPort;
    // }
    m_socket->bind(QHostAddress::AnyIPv4, listenPort , QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_socket, &QUdpSocket::readyRead, this, &PacketReceiver::handleReadyRead);
}

void PacketReceiver::handleReadyRead() {
    qDebug() << "[Receiver] 有数据到达";
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        emit packetReceived(datagram, sender, senderPort);
    }
}
