#include <QUdpSocket>
#include <QTcpSocket>
#include <QtGlobal>
#include <QVariant>
#include <QRandomGenerator>
#include "traffic_generator.h"

TrafficGenerator::TrafficGenerator(QObject *parent)
    : QObject(parent), m_udpSocket(nullptr), m_tcpSocket(nullptr), m_feedbackUdpSocket(nullptr), m_sequence(0), m_protocol(UDP)
{
    // 初始化反馈接收Socket（暂不绑定，等待端口设置后绑定）
    m_feedbackUdpSocket = new QUdpSocket(this);
}

HighResClock* TrafficGenerator::getClock()
{
    return &m_clock;
}

bool TrafficGenerator::configureSocket(PacketType type, TransportProtocol protocol, const QHostAddress &address, quint16 port, quint16 sourcePort)
{
    m_protocol = protocol;
    m_multicastGroup = address;
    m_multicastPort = port;

    if (m_udpSocket) {
        m_udpSocket->deleteLater();
        m_udpSocket = nullptr;
    }
    if (m_tcpSocket) {
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }

    if (protocol == UDP) {
        m_udpSocket = new QUdpSocket(this);
        bool ok = m_udpSocket->bind(QHostAddress::AnyIPv4, sourcePort, QUdpSocket::ShareAddress);
        qDebug() << "UDP绑定端口" << sourcePort << (ok ? "成功" : "失败") << m_udpSocket->errorString();
        if (ok) {
            quint16 actualPort = m_udpSocket->localPort();
            qDebug() << "UDP实际绑定端口：" << actualPort;
        }

        m_multicastGroup = address;
        m_multicastPort = port;
        // 新增：绑定反馈接收Socket（使用用户设置的端口）
        if (m_feedbackPort != 0) {
            bool feedbackOk = m_feedbackUdpSocket->bind(QHostAddress::AnyIPv4, m_feedbackPort, QUdpSocket::ShareAddress);
            qDebug() << "UDP反馈接收端口绑定" << m_feedbackPort << (feedbackOk ? "成功" : "失败")
                     << m_feedbackUdpSocket->errorString();
        }
        // 核心修改：反馈Socket直接绑定到onUdpReadyRead（处理反馈的函数）
        connect(m_feedbackUdpSocket, &QUdpSocket::readyRead,
                this, &TrafficGenerator::onUdpReadyRead);

        if (type == Multicast) {
            m_udpSocket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
            m_udpSocket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, true);
        }
    } else if (protocol == TCP) {
        m_tcpSocket = new QTcpSocket(this);
        if (sourcePort != 0 && !m_tcpSocket->bind(QHostAddress::AnyIPv4, sourcePort)) {
            qWarning() << "TCP绑定失败 (port:" << sourcePort << "):" << m_tcpSocket->errorString();
            return false;
        }
        m_tcpSocket->connectToHost(address, port);
        if (!m_tcpSocket->waitForConnected(5000)) {
            qWarning() << "TCP连接失败:" << m_tcpSocket->errorString();
            return false;
        }
        // 新增：连接 readyRead 信号，监听TCP反馈数据
        connect(m_tcpSocket, &QTcpSocket::readyRead, this, &TrafficGenerator::onTcpReadyRead);
    }
    qDebug() << "Socket配置完成: 协议 =" << (protocol == UDP ? "UDP" : "TCP")
             << "源端口 =" << sourcePort << "目标 =" << address.toString() << ":" << port <<"反馈接收端口:" << m_feedbackPort;
    return true;
}

QByteArray TrafficGenerator::generatePacket(quint32 payloadSize, quint8 priority, quint32 totalPacketCount, quint32 batchNumber)
{
    QByteArray payload;
    payload.resize(payloadSize);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = QRandomGenerator::global()->bounded(256);
    }

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);

    quint64 seq = m_sequence++;
    qint64 timestamp = m_clock.currentTime();
    quint8 packetType = 0; // 普通数据包
    stream << quint16(0xAA55)
           << seq
           << timestamp
           << quint32(payloadSize)
           << packetType
           << priority
           << totalPacketCount // 新增字段，总包数
           << batchNumber;  //  添加批次号字段

    packet.append(payload);
    quint16 crc = qChecksum(packet.constData(), packet.size());
    stream << crc;
    // 解析包头打印，确认写入是否正确
    QDataStream verifyStream(packet);
    quint16 verifyHeader;
    quint64 verifySeq;
    qint64 verifyTimestamp;
    quint32 verifyPayloadSize;
    quint8 verifyPacketType;
    quint8 verifyPriority;
    quint32 verifyTotalPacketCount;
    quint32 verifyBatchNumber;

    verifyStream >> verifyHeader >> verifySeq >> verifyTimestamp >> verifyPayloadSize
        >> verifyPacketType >> verifyPriority >> verifyTotalPacketCount >> verifyBatchNumber;

    qDebug() << "生成包头:"
             << "Header:" << QString::number(verifyHeader, 16)
             << "Seq:" << verifySeq
             << "Timestamp:" << verifyTimestamp
             << "PayloadSize:" << verifyPayloadSize
             << "PacketType:" << verifyPacketType
             << "Priority:" << verifyPriority
             << "TotalPacketCount:" << verifyTotalPacketCount
             << "BatchNumber:" << verifyBatchNumber;
    return packet;
}

QByteArray TrafficGenerator::generateFilePacket(QFile &file, quint32 payloadSize, bool &isLastPacket,
                                                bool isFirstPacket, const QString &fileName, quint64 fileSize,
                                                quint8 priority, quint32 totalPacketCount, quint32 batchNumber)
{
    QByteArray payload;
    if (file.isOpen()) {
        if (isFirstPacket) {
            QDataStream payloadStream(&payload, QIODevice::WriteOnly);
            payloadStream.setByteOrder(QDataStream::LittleEndian);
            payloadStream << fileSize << fileName;
            qDebug() << "生成首个文件数据包: 文件大小=" << fileSize << ", 文件名=" << fileName
                     << ", 有效载荷大小=" << payload.size() << ", 有效载荷十六进制=" << payload.toHex();
        } else {
            payload = file.read(payloadSize);
            isLastPacket = file.atEnd();
            qDebug() << "生成文件数据包: 有效载荷大小=" << payload.size() << ", 是否最后数据包=" << isLastPacket;
        }
    } else {
        isLastPacket = true;
        return QByteArray();
    }

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);

    quint64 seq = m_sequence++;
    qint64 timestamp = m_clock.currentTime();
    quint8 packetType = 1; // 文件数据包
    stream << quint16(0xAA55)
           << seq
           << timestamp
           << quint32(payload.size())
           << packetType
           << priority
           << totalPacketCount // 新增字段，总包数
           << batchNumber;  // ✅ 批次号


    packet.append(payload);
    quint16 crc = qChecksum(packet.constData(), packet.size());
    QDataStream crcStream(&packet, QIODevice::WriteOnly | QIODevice::Append);
    crcStream << crc;
    qDebug() << "生成数据包: CRC=" << crc;
    qDebug() << "生成数据包: 总大小=" << packet.size() << ", 有效载荷大小=" << payload.size();

    // 解析包头打印，确认写入是否正确
    QDataStream verifyStream(packet);
    quint16 verifyHeader;
    quint64 verifySeq;
    qint64 verifyTimestamp;
    quint32 verifyPayloadSize;
    quint8 verifyPacketType;
    quint8 verifyPriority;
    quint32 verifyTotalPacketCount;
    quint32 verifyBatchNumber;

    verifyStream >> verifyHeader >> verifySeq >> verifyTimestamp >> verifyPayloadSize
        >> verifyPacketType >> verifyPriority >> verifyTotalPacketCount >> verifyBatchNumber;


    qDebug() << "生成包头:"
             << "Header:" << QString::number(verifyHeader, 16)
             << "Seq:" << verifySeq
             << "Timestamp:" << verifyTimestamp
             << "PayloadSize:" << verifyPayloadSize
             << "PacketType:" << verifyPacketType
             << "Priority:" << verifyPriority
             << "TotalPacketCount:" << verifyTotalPacketCount
             << "BatchNumber:" << verifyBatchNumber;
    return packet;
}

bool TrafficGenerator::sendPacket(const QByteArray &packet, const QHostAddress &dest, quint16 port, quint64 delayNs)
{
    if (m_protocol == UDP && !m_udpSocket) {
        qWarning() << "UDP Socket 未初始化";
        return false;
    }
    if (m_protocol == TCP && !m_tcpSocket) {
        qWarning() << "TCP Socket 未初始化";
        return false;
    }

    QDataStream stream(packet);
    quint16 header;
    quint64 seq;
    qint64 timestamp;
    quint32 payloadSize;
    quint8 packetType;
    quint8 priority;
    quint32 totalPacketCount;
    quint32 batchNumber;
    stream >> header >> seq >> timestamp >> payloadSize >> packetType >> priority >> totalPacketCount >> batchNumber;
    // 重新生成数据包，更新时间戳
    timestamp = m_clock.currentTime() - delayNs;

    QByteArray updatedPacket;
    QDataStream updatedStream(&updatedPacket, QIODevice::WriteOnly);

    updatedStream << header << seq << timestamp << payloadSize << packetType << priority << totalPacketCount << batchNumber ;
    updatedPacket.append(packet.mid(32, payloadSize)); // 调整偏移量以包含优先级字段
    quint16 newCrc = qChecksum(updatedPacket.constData(), updatedPacket.size());
    QByteArray crcData;
    QDataStream crcStream(&crcData, QIODevice::WriteOnly);
    crcStream << newCrc;
    updatedPacket.append(crcData);

    // 验证数据包大小
    qint64 expectedSize = static_cast<qint64>(sizeof(quint16) + sizeof(quint64) + sizeof(qint64) +
                                              sizeof(quint32) + sizeof(quint8) + sizeof(quint8)+
                                              sizeof(quint32) + sizeof(quint32) +
                                              payloadSize + sizeof(quint16));
    if (updatedPacket.size() != expectedSize) {
        qWarning() << "数据包大小错误: 实际=" << updatedPacket.size() << ", 预期=" << expectedSize;
        return false;
    }

    qDebug() << "发送数据包: 大小=" << updatedPacket.size() << ", 有效载荷大小=" << payloadSize << ", CRC=" << newCrc;

    bool success = false;
    if (m_protocol == UDP) {
        qint64 bytesWritten = m_udpSocket->writeDatagram(updatedPacket, dest, port);
        success = (bytesWritten == updatedPacket.size());
        if (!success) {
            qWarning() << "UDP数据包发送失败: " << m_udpSocket->errorString();
        }
    } else if (m_protocol == TCP) {
        if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) {
            qDebug() << "TCP套接字未连接，尝试重新连接: " << dest.toString() << ":" << port;
            m_tcpSocket->connectToHost(dest, port);
            if (!m_tcpSocket->waitForConnected(10000)) {
                qWarning() << "TCP重新连接失败: " << m_tcpSocket->errorString();
                return false;
            }
        }
        QByteArray packetSizeData;
        QDataStream packetSizeStream(&packetSizeData, QIODevice::WriteOnly);
        packetSizeStream << quint32(updatedPacket.size());
        qint64 bytesWritten = m_tcpSocket->write(packetSizeData);
        if (bytesWritten != packetSizeData.size()) {
            qWarning() << "发送数据包大小失败: 写入=" << bytesWritten << ", 预期=" << packetSizeData.size() << ", 错误=" << m_tcpSocket->errorString();
            return false;
        }

        bytesWritten = m_tcpSocket->write(updatedPacket);
        if (bytesWritten != updatedPacket.size()) {
            qWarning() << "发送数据包失败: 写入=" << bytesWritten << ", 预期=" << updatedPacket.size() << ", 错误=" << m_tcpSocket->errorString();
            return false;
        }

        m_tcpSocket->flush();
        success = true;
    }

    if (success) {
        qDebug() << "数据包发送成功, 序列号:" << seq << "时间戳:" << timestamp << "目标:" << dest.toString() << ":" << port;
        m_sentPackets.append(qMakePair(seq, timestamp));
        return true;
    } else {
        qWarning() << "数据包发送失败:" << (m_protocol == UDP ? m_udpSocket->errorString() : m_tcpSocket->errorString());
        return false;
    }
}

void TrafficGenerator::resetSequence()
{
    m_sequence = 0;
}

QVector<QPair<qint64, qint64>> TrafficGenerator::getSentPackets()
{
    return m_sentPackets;
}

void TrafficGenerator::clearSentPackets()
{
    m_sentPackets.clear();
}

void TrafficGenerator::onTcpReadyRead()
{
    qDebug() << "进入 onTcpReadyRead()，可读字节数：" << m_tcpSocket->bytesAvailable();
    while (m_tcpSocket->bytesAvailable() >= 16) {
        QByteArray feedbackData = m_tcpSocket->peek(16);

        QDataStream in(feedbackData);
        in.setByteOrder(QDataStream::BigEndian);

        quint32 magic;
        in >> magic;

        if (magic != 0xFEEDC0DE) {
            qWarning() << "[A端] 收到非法反馈包，magic不符";
            // 跳过一个字节继续尝试同步
            m_tcpSocket->read(1);
            continue;
        }

        // magic合法，读取整个16字节包
        feedbackData = m_tcpSocket->read(16);
        qDebug() << "原始数据：" << feedbackData.toHex();
        emit feedbackReceivedTCP(feedbackData);
    }
}
void TrafficGenerator::onUdpReadyRead()
{
    while (m_feedbackUdpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_feedbackUdpSocket->pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort;
        if (m_feedbackUdpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort) == -1) {
            qWarning() << "读取UDP Datagram失败";
            continue;
        }

        if (datagram.size() < 16) {
            qWarning() << "[A端] UDP反馈包太小，丢弃";
            continue;
        }

        QDataStream in(datagram);
        in.setByteOrder(QDataStream::BigEndian);

        quint32 magic;
        in >> magic;

        if (magic != 0xFEEDC0DE) {
            qWarning() << "[A端] UDP反馈包魔数错误，丢弃";
            continue;
        }

        // 反馈包大小固定16字节，确认无误，直接发射信号
        qDebug() << "[A端] 收到UDP反馈包，数据：" << datagram.toHex();
        emit feedbackReceivedUDP(datagram);
    }
}


