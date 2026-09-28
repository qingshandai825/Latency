#include "tsm_controller.h"
#include "traffic_generator.h"
#include "packet_scheduler.h"
#include <QDebug>
#include <QFile>
#include <QThread>
#include <QFileInfo>
#include <QtEndian>

TSM_Controller::TSM_Controller(QObject *parent)
    : QObject(parent),
    m_generator(new TrafficGenerator(this)),
    m_scheduler(new PacketScheduler(this)),
    m_delayInjector(new DelayInjector(this))
{
    connect(m_scheduler, &PacketScheduler::sendImmediately,
            m_generator, &TrafficGenerator::sendPacket);
    connect(m_scheduler, &PacketScheduler::sendImmediately,
            this, &TSM_Controller::onPacketSentByScheduler);
    connect(m_generator, &TrafficGenerator::feedbackReceivedTCP,
            this, &TSM_Controller::onFeedbackReceivedTCP);
    connect(m_generator, &TrafficGenerator::feedbackReceivedUDP,
            this, &TSM_Controller::onFeedbackReceivedUDP);
}

void TSM_Controller::setSchedulingAlgorithm(PacketScheduler::SchedulingAlgorithm algorithm) {
    m_scheduler->setSchedulingAlgorithm(algorithm);
}

void TSM_Controller::setPriorityDistribution(PacketScheduler::PriorityDistribution distribution) {
    m_scheduler->setPriorityDistribution(distribution);
}

void TSM_Controller::configureTest(const QVariantMap &params)
{
    m_currentConfig = params;
    m_targets.clear();



    TrafficGenerator::PacketType type;
    TrafficGenerator::TransportProtocol protocol = TrafficGenerator::UDP;
    QString typeStr = params["type"].toString();
    if (typeStr == "单播") {
        type = TrafficGenerator::Unicast;
        if (params.contains("protocol") && params["protocol"].toString().toLower() == "tcp") {
            protocol = TrafficGenerator::TCP;
        }
    } else if (typeStr == "多播") {
        type = TrafficGenerator::Multicast;
    } else {
        type = TrafficGenerator::Broadcast;
    }

    if (type == TrafficGenerator::Multicast) {
        QHostAddress multicastGroup(params["multicastGroup"].toString());
        quint16 multicastPort = params["multicastPort"].toUInt();
        if (!multicastGroup.isMulticast()) {
            qWarning() << "无效的组播地址";
            emit testStatusChanged("组播地址无效");
            return;
        }
        if (!m_generator->configureSocket(type, protocol, multicastGroup, multicastPort)) {
            qWarning() << "组播 socket 配置失败";
            emit testStatusChanged("组播配置失败");
            return;
        }
        m_targets.append({multicastGroup, multicastPort});
    }
    else if (type == TrafficGenerator::Broadcast) {
        QString addrStr = params.contains("broadcastAddress") ? params["broadcastAddress"].toString().trimmed() : "255.255.255.255";
        QHostAddress broadcastAddress(addrStr.isEmpty() ? "255.255.255.255" : addrStr);

        bool ok = false;
        quint16 port = params["port"].toString().toUShort(&ok);
        if (!ok || port == 0) {
            qWarning() << "广播端口无效:" << params["port"];
            emit testStatusChanged("广播端口无效");
            return;
        }

        if (broadcastAddress.isNull()) {
            qWarning() << "无效的广播地址:" << broadcastAddress.toString();
            emit testStatusChanged("广播地址无效");
            return;
        }

        if (!m_generator->configureSocket(type, protocol, broadcastAddress, port)) {
            qWarning() << "广播 socket 配置失败";
            emit testStatusChanged("广播配置失败");
            return;
        }

        m_targets.append({broadcastAddress, port});
    }
    else if (params.contains("targets")) {
        QVariantList targetList = params["targets"].toList();
        if (!targetList.isEmpty()) {
            QVariantList firstTarget = targetList.first().toList();
            QHostAddress address(firstTarget[0].toString());
            quint16 port = firstTarget[1].toUInt();
            if (!m_generator->configureSocket(type, protocol, address, port)) {
                qWarning() << "单播 socket 配置失败";
                emit testStatusChanged("配置失败");
                return;
            }
        }
        for (const QVariant &v : targetList) {
            QVariantList pair = v.toList();
            if (pair.size() == 2) {
                QHostAddress addr(pair[0].toString());
                quint16 port = pair[1].toUInt();
                if (!addr.isNull() && port > 0) {
                    m_targets.append({addr, port});
                }
            }
        }
    } else {
        qWarning() << "无效的配置参数";
        emit testStatusChanged("配置无效");
        return;
    }

    m_generator->clearSentPackets();
    m_generator->resetSequence();
    emit testStatusChanged("已配置");
}

void TSM_Controller::startTest()
{
    m_batchTest++;
    if (m_currentConfig.isEmpty()) {
        qWarning() << "测试未配置";
        emit testStatusChanged("未配置");
        return;
    }

    m_startTime = m_clock.currentTime();
    m_sentPacketCount = 0;
    quint32 totalPackets = m_currentConfig["packetCount"].toUInt();
    m_expectedPacketCount = totalPackets;
    m_isTcpMode = (m_currentConfig.contains("protocol") &&
                   m_currentConfig["protocol"].toString().toLower() == "tcp");

    quint32 packetSize = m_currentConfig["packetSize"].toUInt();
    quint32 intervalMs = qMax(m_currentConfig["interval"].toUInt(), 1U);
    qint64 intervalNs = intervalMs * 1000000LL;
    qint64 delayNs = m_delayInjector->calculateDelay();

    HighResClock* clock = m_generator->getClock();
    if (!clock) {
        qWarning() << "时钟不可用";
        emit testStatusChanged("时钟错误");
        return;
    }

    qint64 baseTime = clock->currentTime();

    qint64 totalElapsedNs = 0;

    for (quint32 i = 0; i < totalPackets; ++i) {
        quint8 priority = m_scheduler->generatePriority();

        if (m_isTcpMode) {
            QElapsedTimer timer;
            timer.start();

            QByteArray packet = m_generator->generatePacket(packetSize, priority, totalPackets, m_batchTest);
            qint64 sendTime = baseTime + (i * intervalNs);

            for (const auto &target : m_targets) {
                m_scheduler->schedulePacket(sendTime, packet, target.first, target.second, delayNs, priority);
            }

            qint64 elapsed = timer.nsecsElapsed();
            totalElapsedNs += elapsed;

            qDebug() << QString("TCP Packet #%1 构造+发送耗时: %2 ns").arg(i).arg(elapsed);
        } else {
            QByteArray packet = m_generator->generatePacket(packetSize, priority, totalPackets, m_batchTest);
            qint64 sendTime = baseTime + (i * intervalNs);
            for (const auto &target : m_targets) {
                m_scheduler->schedulePacket(sendTime, packet, target.first, target.second, delayNs, priority);
            }
        }
    }

    if (m_isTcpMode) {
        double totalMs = totalElapsedNs / 1e6;
        m_testTotalMs = totalMs; // 记录普通TCP测试总时长
        emit testStatusChanged(QString("TCP 测试完成，%1 个包构造+发送总时长: %2 ms")
                                   .arg(totalPackets).arg(totalMs, 0, 'f', 2));
        // TCP测试完成后检查时延
        checkDelayAbnormal(static_cast<qint64>(totalMs));
    } else {
        emit testStatusChanged("UDP 测试运行中");
    }
}


void TSM_Controller::sendFile(const QVariantMap &params)
{
    m_batchFile++;
    QString filePath = params["filePath"].toString();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开文件:" << filePath;
        emit testStatusChanged("无法打开文件");
        return;
    }

    quint32 packetSize = params["packetSize"].toUInt();
    quint32 intervalMs = qMax(params["interval"].toUInt(), 1U);
    qint64 intervalNs = intervalMs * 1000000LL;
    qint64 delayNs = m_delayInjector->calculateDelay();
    qDebug()<<"delayNsfawenjian:"<<delayNs;
    quint64 fileSize = file.size();
    QString fileName = QFileInfo(filePath).fileName();

    HighResClock* clock = m_generator->getClock();
    if (!clock) {
        qWarning() << "时钟不可用";
        emit testStatusChanged("时钟错误");
        return;
    }
    m_startTime = m_clock.currentTime();
    m_sentPacketCount = 0;

    // 粗略估算包数（首包 + 文件数据包总数）
    quint32 totalPackets = static_cast<quint32>(fileSize / packetSize + 2);
    m_expectedPacketCount = totalPackets;
    m_isTcpMode = m_currentConfig.contains("protocol") &&
                  m_currentConfig["protocol"].toString().toLower() == "tcp";

    bool isTcpMode = m_isTcpMode;
    qint64 baseTime = clock->currentTime();
    quint32 packetIndex = 0;
    bool isLastPacket = false;

    // 发送首个数据包（带文件头）
    quint8 priority = m_scheduler->generatePriority();
    QByteArray packet = m_generator->generateFilePacket(file, packetSize, isLastPacket, true, fileName, fileSize, priority, totalPackets, m_batchFile);
    if (!packet.isEmpty()) {
        if (isTcpMode) {
            for (const auto &target : m_targets) {
                if (!m_generator->sendPacket(packet, target.first, target.second, delayNs)) {
                    qWarning() << "TCP发送文件首包失败";
                    emit testStatusChanged("发送失败");
                    file.close();
                    return;
                }
            }
        } else {
            qint64 sendTime = baseTime;
            for (const auto &target : m_targets) {
                m_scheduler->schedulePacket(sendTime, packet, target.first, target.second, delayNs, priority);
            }
        }
        packetIndex++;
    }

    // 发送后续数据包
    while (!isLastPacket) {
        priority = m_scheduler->generatePriority();
        packet = m_generator->generateFilePacket(file, packetSize, isLastPacket, false, QString(), 0, priority, totalPackets, m_batchFile);
        if (packet.isEmpty() && isLastPacket) {
            break;
        }

        if (isTcpMode) {
            for (const auto &target : m_targets) {
                if (!m_generator->sendPacket(packet, target.first, target.second, delayNs)) {
                    qWarning() << "TCP发送文件数据包失败";
                    emit testStatusChanged("发送失败");
                    file.close();
                    return;
                } else {
                    m_sentPacketCount++;
                }
            }
        } else {
            qint64 sendTime = baseTime + (packetIndex * intervalNs);
            for (const auto &target : m_targets) {
                m_scheduler->schedulePacket(sendTime, packet, target.first, target.second, delayNs, priority);
            }
        }

        packetIndex++;
        if (isTcpMode) {
            QThread::usleep(100);
        }
    }

    file.close();

    if (isTcpMode) {
        qint64 endTime = clock->currentTime();
        qint64 diffNs = endTime - m_startTime;
        totalDuration = diffNs / 1'000'000.0;
        m_fileTotalDuration = totalDuration; // 计算文件传输总时长
        emit testStatusChanged(QString("文件发送完成(TCP)，总耗时：%1 ms").arg(totalDuration));
        QVector<QPair<qint64, qint64>> packets = m_generator->getSentPackets();
        emit packetSent(packets);
        // TCP文件传输完成后检查时延
        checkDelayAbnormal(totalDuration);
    } else {
        emit testStatusChanged("文件发送中(UDP)");
    }
}

void TSM_Controller::stopTest()
{
    QVector<QPair<qint64, qint64>> packets = m_generator->getSentPackets();
    emit packetSent(packets);
    emit testStatusChanged("测试已停止");
    m_generator->resetSequence();
}

void TSM_Controller::onPacketSentByScheduler(const QByteArray &, const QHostAddress &, quint16, quint64)
{
    if (m_isTcpMode) return;

    m_sentPacketCount++;
    if (m_sentPacketCount == m_expectedPacketCount) {
        qint64 endTime = m_clock.currentTime();
        qint64 diffNs = endTime - m_startTime; // 纳秒级差值
        m_totalDuration = diffNs / 1'000'000.0; // 转换为毫秒（保留多位小数） // 存入成员变量
        // 检查UDP时延是否异常
        checkDelayAbnormal(m_totalDuration);
        qDebug() << QString("UDP所有数据包发送完成，总处理时延= %1 ms").arg(m_totalDuration);
        emit testStatusChanged(QString("UDP发送完成，总时延：%1 ms").arg(m_totalDuration));
    }
}

void TSM_Controller::onFeedbackReceivedTCP(const QByteArray &feedbackData)
{
    if (feedbackData.size() < 16) {
        qWarning() << "反馈数据长度不足16字节，实际为" << feedbackData.size();
        return;
    }

    QDataStream stream(feedbackData);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 magic;
    quint32 batchNumber;
    quint32 cDurationInt;
    quint32 bDurationInt;

    stream >> magic >> batchNumber >> cDurationInt >> bDurationInt;

    if (magic != 0xFEEDC0DE) {
        qWarning() << "收到反馈包魔数不匹配：" << QString::number(magic, 16).toUpper();
        return;
    }

    // 转换大端整数为本机float
    cDurationInt = qFromBigEndian(cDurationInt);
    bDurationInt = qFromBigEndian(bDurationInt);

    float cDurationMs, bDurationMs;
    memcpy(&cDurationMs, &cDurationInt, sizeof(float));
    memcpy(&bDurationMs, &bDurationInt, sizeof(float));

    // 区分TCP文件传输和普通TCP测试，使用对应的A端时长
    double aDuration = 0;
    if (batchNumber == m_batchFile) {
        aDuration = m_fileTotalDuration;
    } else if (batchNumber == m_batchTest) {
        aDuration = m_testTotalMs;
    } else {
        qWarning() << "反馈批次号不匹配，无法确定A端时长";
        aDuration = -1;
    }

    qDebug() << QString("收到反馈包：批次 %1, C 耗时 %2 ms, B 耗时 %3 ms")
                    .arg(batchNumber)
                    .arg(cDurationMs, 0, 'f', 2)
                    .arg(bDurationMs, 0, 'f', 2);

    emit testStatusChanged(
        QString("批次 %1 总反馈：A端处理时间 %2 ms, B端处理时间 %3 ms，C端处理时间 %4 ms")
            .arg(batchNumber)
            .arg(aDuration, 0, 'f', 2)
            .arg(bDurationMs, 0, 'f', 2)
            .arg(cDurationMs, 0, 'f', 2));
    // 新增：发送TCP时延统计数据
    emit delayStatsUpdatedTCP(aDuration, bDurationMs, cDurationMs);
}

void TSM_Controller::onFeedbackReceivedUDP(const QByteArray &feedbackData)
{
    if (feedbackData.size() < 16) {
        qWarning() << "反馈数据长度不足16字节，实际为" << feedbackData.size();
        return;
    }

    QDataStream stream(feedbackData);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 magic;
    quint32 batchNumber;
    quint32 cDurationInt;
    quint32 bDurationInt;

    stream >> magic >> batchNumber >> cDurationInt >> bDurationInt;

    if (magic != 0xFEEDC0DE) {
        qWarning() << "收到反馈包魔数不匹配：" << QString::number(magic, 16).toUpper();
        return;
    }

    // 转大端整数转本机float
    cDurationInt = qFromBigEndian(cDurationInt);
    bDurationInt = qFromBigEndian(bDurationInt);

    float cDurationMs, bDurationMs;
    memcpy(&cDurationMs, &cDurationInt, sizeof(float));
    memcpy(&bDurationMs, &bDurationInt, sizeof(float));

    qDebug() << QString("收到反馈包：批次 %1, C 耗时 %2 ms, B 耗时 %3 ms")
                    .arg(batchNumber)
                    .arg(cDurationMs, 0, 'f', 2)
                    .arg(bDurationMs, 0, 'f', 2);

    emit testStatusChanged(
        QString("批次 %1 总反馈：A端处理时间 %2 ms, B端处理时间 %3 ms，C端处理时间 %4 ms")
            .arg(batchNumber)
            .arg(m_totalDuration, 0, 'f', 2)
            .arg(bDurationMs, 0, 'f', 2)
            .arg(cDurationMs, 0, 'f', 2));
    // 新增：发送UDP时延统计数据
    emit delayStatsUpdatedUDP(m_totalDuration, bDurationMs, cDurationMs);
}

void TSM_Controller::checkDelayAbnormal(qint64 totalDuration) {
    if (totalDuration > m_delayThreshold) {
        // 总时延超过阈值，递增异常次数
        m_abnormalDelayCount++;
        emit testStatusChanged(QString("⚠️ 总时延%1 ms超过阈值%2 ms，当前连续异常次数：%3/%4")
                                   .arg(totalDuration)
                                   .arg(m_delayThreshold)
                                   .arg(m_abnormalDelayCount)
                                   .arg(m_abnormalCountThreshold));

        // 当连续异常次数超过阈值时，触发告警
        if (m_abnormalDelayCount >= m_abnormalCountThreshold) {
            emit delayAlarmTriggered(totalDuration, m_delayThreshold, m_abnormalDelayCount);
            emit testStatusChanged(QString("🚨 时延告警！总时延%1 ms连续%2次超过阈值%3 ms")
                                       .arg(totalDuration)
                                       .arg(m_abnormalDelayCount)
                                       .arg(m_delayThreshold));
        }
    } else {
        // 总时延正常，重置异常次数
        if (m_abnormalDelayCount > 0) {
            emit testStatusChanged(QString("✅ 总时延%1 ms恢复正常，连续异常次数已重置")
                                       .arg(totalDuration));
            m_abnormalDelayCount = 0;
        }
    }
}
