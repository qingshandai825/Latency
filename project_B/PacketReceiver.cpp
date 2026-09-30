#include <PacketReceiver.h>
#include <QDataStream>
#include <QDebug>
#include <QBuffer>
#include <QtEndian>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QRandomGenerator>
#include <QUdpSocket>
#include <chrono>
#include <QThread>
#include <QMap>

PacketReceiver::PacketReceiver(QObject *parent)
    : QObject(parent),
    abnormalThreshold(500),
    udpForwardSocket(nullptr),
    currentBatchNumber(0),
    batchPacketCount(0),
    batchTotalPacketCount(0),
    batchStartTimeNs(0),
    appendTimeNs(0),
    delayInjector(nullptr)// 新增：用于记录 append 函数的累计耗时
{
    udpForwardSocket = new QUdpSocket(this);
    udpForwardSocket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
}

qint64 currentSystemTimeNs()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

void PacketReceiver::setDelayInjector(DelayInjector *injector)
{
    delayInjector = injector;
}

void PacketReceiver::appendData(const QByteArray &data)
{
    qint64 startAppendNs = currentSystemTimeNs(); // 记录开始时间
    tcpBuffer.append(data);
    processBuffer();
    qint64 endAppendNs = currentSystemTimeNs(); // 记录结束时间
    appendTimeNs += (endAppendNs - startAppendNs); // 累加耗时
}

void PacketReceiver::appendUdpData(const QByteArray &data)
{
    qint64 startAppendNs = currentSystemTimeNs(); // 记录开始时间
    udpBuffer.append(data);
    processUdpBuffer();
    qint64 endAppendNs = currentSystemTimeNs(); // 记录结束时间
    appendTimeNs += (endAppendNs - startAppendNs); // 累加耗时
}

void PacketReceiver::processBuffer()
{
    qDebug() << "[processBuffer] enter, tcpBuffer.size()=" << tcpBuffer.size();
    constexpr int MAGIC_SIZE = 2;
    constexpr int SEQ_SIZE = 8;
    constexpr int TIMESTAMP_SIZE = 8;
    constexpr int PAYLOAD_SIZE_FIELD = 4;
    constexpr int TYPE_SIZE = 1;
    constexpr int PRIORITY_SIZE = 1;
    constexpr int TotalPacketCount = 4;
    constexpr int BatchNumber = 4;
    constexpr int CRC_SIZE = 2;
    constexpr int HEADER_SIZE = MAGIC_SIZE + SEQ_SIZE + TIMESTAMP_SIZE + PAYLOAD_SIZE_FIELD + TYPE_SIZE + PRIORITY_SIZE + TotalPacketCount + BatchNumber;

    const quint32 MAX_PAYLOAD_SIZE = 64 * 1024 * 1024;

    // 收集所有解析出的数据包
    QList<QByteArray> processedPackets;

    while (tcpBuffer.size() >= 4) {
        quint32 packetLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(tcpBuffer.constData()));

        if (packetLen == 0 || packetLen > MAX_PAYLOAD_SIZE + HEADER_SIZE + CRC_SIZE) {
            qWarning() << "无效的TCP数据包长度(超限或为0):" << packetLen << ", 丢弃一个字节重新同步";
            tcpBuffer.remove(0, 1);
            continue;
        }

        if (tcpBuffer.size() < static_cast<int>(4 + packetLen)) {
            break; // 等待更多数据
        }

        const qint64 packetWorkStartNs = currentSystemTimeNs();
        // 1. 提取原始包体（包含头部、负载、CRC）
        QByteArray packetData = tcpBuffer.mid(4, packetLen);

        if (packetData.size() < HEADER_SIZE + CRC_SIZE) {
            qWarning() << "包体长度不足，丢弃该包";
            tcpBuffer.remove(0, 4 + packetLen);
            continue;
        }

        quint16 magic = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(packetData.constData()));
        if (magic != 0xAA55) {
            qWarning() << "包头Magic错误，丢弃包头，重新同步";
            tcpBuffer.remove(0, 1);
            continue;
        }

        // 2. 读取原始字段（后续需用更新后的aTimestamp）
        quint64 seq = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(packetData.constData() + 2));
        quint32 payloadSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packetData.constData() + 18));
        quint8 packetType = static_cast<quint8>(packetData[22]);
        quint8 priority = static_cast<quint8>(packetData[23]);
        quint32 totalPacketCount = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packetData.constData() + 24));
        quint32 batchNumber = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packetData.constData() + 28));

        // 3. 验证包长度合法性（避免后续内存越界）
        if (payloadSize > MAX_PAYLOAD_SIZE || packetLen != HEADER_SIZE + payloadSize + CRC_SIZE) {
            qWarning() << "负载大小异常或包长不匹配，丢弃该包";
            tcpBuffer.remove(0, 4 + packetLen);
            continue;
        }

        // -------------------------- 核心修改开始 --------------------------
        // 4. 生成新的aTimestamp（当前系统时间，纳秒级）
        //qint64 newATimestamp = m_clock.currentTime();
        qint64 newATimestamp =m_clock.currentTime();
        if (delayInjector) {
            qint64 delayNs = delayInjector->calculateDelay();
            newATimestamp -= delayNs; // 减去计算的延迟
            qDebug() << "！！！！aaa" << delayNs;

        }
        // 5. 将新aTimestamp转换为大端字节序，覆盖packetData中10字节处的原始aTimestamp
        qToBigEndian(newATimestamp, reinterpret_cast<uchar*>(packetData.data() + 10));
        // 6. 重新计算CRC（头部+负载，共 HEADER_SIZE + payloadSize 字节）
        quint16 newCRC = qChecksum(packetData.constData(), HEADER_SIZE + payloadSize);
        // 7. 将新CRC转换为大端字节序，覆盖packetData中原始CRC字段（头部+负载之后）
        qToBigEndian(newCRC, reinterpret_cast<uchar*>(packetData.data() + HEADER_SIZE + payloadSize));
        // -------------------------- 核心修改结束 --------------------------

        // 8. 读取更新后的aTimestamp（用于后续延迟计算和日志）
        qint64 aTimestamp = newATimestamp; // 直接使用刚才生成的新时间戳，避免重复解析
        processBatchInfo(batchNumber, totalPacketCount, currentSystemTimeNs() - packetWorkStartNs);

        // 9. 计算延迟（基于更新后的aTimestamp）
        qint64 bTimestamp = currentSystemTimeNs();
        double delayMs = (bTimestamp - aTimestamp) / 1e6;
//aTimestamp // 日志显示更新后的时间戳
        qDebug() << "解析TCP包头(已更新aTimestamp):"
                 << "Magic:" << QString::number(magic, 16)
                 << "Seq:" << seq
                 << "更新后ATime:" << m_clock.currentTime()
                 << "PayloadSize:" << payloadSize
                 << "Type:" << packetType
                 << "Priority:" << priority
                 << "TotalPacketCount:" << totalPacketCount
                 << "BatchNumber:" << batchNumber
                 << "Delay(ms):" << delayMs;

        emit delayReceived(delayMs, "TCP", aTimestamp);
        // 10. 将更新后的packetData加入处理列表
        processedPackets.append(packetData);

        tcpBuffer.remove(0, 4 + packetLen);
        qDebug() << "[processBuffer] remove, tcpBuffer.size()=" << tcpBuffer.size();
    }

    // 处理完所有包后发出信号
    if (!processedPackets.isEmpty()) {
        emit tcpPacketsProcessed(processedPackets);
    }
}

void PacketReceiver::setAbnormalRepeatThreshold(int n)
{
    maxConsecutiveAbnormal = n;
}

void PacketReceiver::processUdpBuffer()
{
    constexpr int MIN_HEADER_SIZE = 2 + 8 + 8 + 4 + 1 + 1 + 4 + 4; // 固定头部大小（与TCP一致）
    constexpr int CRC_SIZE = 2;                                     // CRC校验字段大小
    constexpr int TIMESTAMP_OFFSET = 10;                            // aTimestamp在头部的偏移（magic(2)+seq(8)=10）

    while (true) {
        // 1. 检查最小数据量（头部+CRC），不足则退出循环等待更多数据
        if (udpBuffer.size() < MIN_HEADER_SIZE + CRC_SIZE) {
            return;
        }

        // 2. 验证Magic字段（0xAA55），不匹配则丢弃1字节重新同步
        quint16 magic = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(udpBuffer.constData()));
        qDebug() << "Magic:" << QString::number(magic, 16);
        if (magic != 0xAA55) {
            qDebug() << "Magic错误，丢弃1字节重新同步";
            udpBuffer.remove(0, 1);
            continue;
        }

        // 3. 读取负载大小，计算完整数据包长度（头部+负载+CRC）
        quint32 payloadSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(udpBuffer.constData() + 18));
        int totalPacketSize = MIN_HEADER_SIZE + payloadSize + CRC_SIZE;

        // 4. 检查完整数据包是否接收完毕，不足则退出等待
        if (udpBuffer.size() < totalPacketSize) {
            qDebug() << "UDP数据包未接收完整，当前大小:" << udpBuffer.size() << "，需等待至:" << totalPacketSize;
            return;
        }

        const qint64 packetWorkStartNs = currentSystemTimeNs();
        // 5. 提取完整数据包（从缓冲区头部截取）
        QByteArray packet = udpBuffer.left(totalPacketSize);

        // -------------------------- 核心修改开始 --------------------------
        // 6. 生成新的aTimestamp（当前系统时间，纳秒级）
        qint64 newATimestamp = m_clock.currentTime();
        if (delayInjector) {
            qint64 delayNs = delayInjector->calculateDelay();
            newATimestamp -= delayNs; // 减去计算的延迟
            //newATimestamp -= 0;
        }
        // 7. 将新aTimestamp转换为大端字节序，覆盖packet中10字节处的原始值
        // packet.data()返回可修改的指针，定位到aTimestamp偏移位置后写入
        qToBigEndian(newATimestamp, reinterpret_cast<uchar*>(packet.data() + TIMESTAMP_OFFSET));
        // 8. 重新计算CRC（范围：头部+负载，共 MIN_HEADER_SIZE + payloadSize 字节）
        quint16 newCRC = qChecksum(packet.constData(), MIN_HEADER_SIZE + payloadSize);
        // 9. 将新CRC转换为大端字节序，覆盖packet中原始CRC字段（头部+负载之后）
        qToBigEndian(newCRC, reinterpret_cast<uchar*>(packet.data() + MIN_HEADER_SIZE + payloadSize));
        // -------------------------- 核心修改结束 --------------------------

        // 10. 验证更新后的CRC（可选，用于确认修改正确性，可删除以优化性能）
        quint16 updatedReceivedCrc = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(packet.constData() + MIN_HEADER_SIZE + payloadSize));
        quint16 updatedComputedCrc = qChecksum(packet.constData(), MIN_HEADER_SIZE + payloadSize);
        if (updatedReceivedCrc != updatedComputedCrc) {
            emit logMessage(QString("UDP CRC更新后校验异常: 收到=%1, 计算=%2").arg(updatedReceivedCrc).arg(updatedComputedCrc));
            udpBuffer.remove(0, totalPacketSize);
            continue;
        }

        // 11. 解析批次信息，更新批次状态
        quint32 totalPacketCount = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packet.constData() + 24));
        quint32 batchNumber = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packet.constData() + 28));
        processBatchInfo(batchNumber, totalPacketCount, currentSystemTimeNs() - packetWorkStartNs);

        // 12. 读取更新后的aTimestamp（直接用新生成的值，避免重复解析）
        qint64 aTimestamp = newATimestamp;
        qDebug() << "[UDP] 更新后aTimestamp:" << aTimestamp;

        // 13. 计算延迟（基于更新后的aTimestamp）
        qint64 bTimestamp = currentSystemTimeNs();
        double delayMs = (bTimestamp - aTimestamp) / 1e6;
        qDebug() << "[UDP] bTimestamp (recv):" << bTimestamp;
        qDebug() << "[UDP] 延迟(ms):" << delayMs;
        qDebug() << "[PacketReceiver] 发送delayReceived信号: " << delayMs << aTimestamp;

        // 14. 发送信号（携带更新后的数据包）
        emit delayReceived(delayMs, "UDP", aTimestamp);
        emit packetReceived(packet, 0x11); // 此处packet已包含更新后的aTimestamp和CRC

        // 15. 从缓冲区中移除已处理的数据包
        udpBuffer.remove(0, totalPacketSize);
    }
}

void PacketReceiver::setForwardTarget(const QHostAddress &addr, quint16 port)
{
    forwardAddr = addr;
    forwardPort = port;
}

void PacketReceiver::setAbnormalThreshold(qint64 threshold)
{
    abnormalThreshold = threshold;
}

void PacketReceiver::processBatchInfo(quint32 batchNumber, quint32 totalPacketCount, qint64 localProcessingNs)
{
    qint64 nowNs = currentSystemTimeNs();
    qDebug() << "nowNs:" << nowNs;

    if (currentBatchNumber != batchNumber || batchPacketCount >= batchTotalPacketCount) {
        currentBatchNumber = batchNumber;
        batchTotalPacketCount = totalPacketCount;
        batchPacketCount = 1;
        batchStartTimeNs = nowNs;
        batchProcessingNs = 0;

        qDebug() << "batchStartTimeNs:" << batchStartTimeNs;

        qDebug() << QString("新批次 %1 开始，包总数: %2").arg(batchNumber).arg(totalPacketCount);
    } else {
        batchPacketCount++;
        qDebug() << QString("批次 %1 当前进度: %2/%3")
                        .arg(currentBatchNumber)
                        .arg(batchPacketCount)
                        .arg(batchTotalPacketCount);
    }

    batchProcessingNs += qMax<qint64>(0, localProcessingNs);
    if (batchPacketCount == batchTotalPacketCount && totalPacketCount > 0) {
        // 完整协议包已由 packetReceived/tcpPacketsProcessed 逐包转发。
        // 这里不能把裸 payload 拼成一个 UDP 包，否则 C 端无法按协议解析。

        // 目标速率控制会拉开到包间隔，不能把等待下一个包的时间算作 B 处理耗时。
        double elapsedMs = batchProcessingNs / 1e6;
        emit batchProcessingTime(batchNumber, elapsedMs);
        qDebug() << QString("批次 %1 本地处理耗时 %2 ms").arg(batchNumber).arg(elapsedMs, 0, 'f', 6);
        qDebug()<<"elapsedMs"<<elapsedMs;
        qDebug()<<"appendTimeNs"<<appendTimeNs;

        batchPacketCount = 0;
        batchTotalPacketCount = 0;
        currentBatchNumber = 0;
        batchProcessingNs = 0;
        appendTimeNs = 0; // 重置 append 耗时
    }
}
