#include "tsm_controller.h"
#include "traffic_generator.h"
#include "packet_scheduler.h"
#include <QDebug>
#include <QFile>
#include <QThread>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QtEndian>
#include <limits>

TSM_Controller::TSM_Controller(QObject *parent)
    : QObject(parent),
    m_generator(new TrafficGenerator(this)),
    m_scheduler(new PacketScheduler(this)),
    m_delayInjector(new DelayInjector(this))
{
    m_scheduler->setDispatcher([this](const QByteArray &packet, const QHostAddress &dest,
                                        quint16 port, quint64 delayNs) {
        if (!m_active || m_sendFailure || !m_generator->canAcceptPacket(packet.size() + 4))
            return false;
        QElapsedTimer sendWork;
        sendWork.start();
        if (!m_generator->sendPacket(packet, dest, port, delayNs)) {
            m_sendFailure = true;
            QTimer::singleShot(0, this, [this]() {
                stopTest();
                emit testStatusChanged(QStringLiteral("发送失败，任务已停止"));
            });
            return false;
        }
        m_activeProcessingNs += sendWork.nsecsElapsed();
        ++m_sentPacketCount;
        m_sentBytes += packet.size() + (m_isTcpMode ? 4 : 0);
        return true;
    });
    connect(m_scheduler, &PacketScheduler::packetDispatched,
            this, &TSM_Controller::onPacketDispatched);
    m_producerTimer = new QTimer(this);
    m_producerTimer->setInterval(1);
    connect(m_producerTimer, &QTimer::timeout, this, &TSM_Controller::pumpPackets);
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
    if (m_active) stopTest();
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
    emit testStatusChanged("已配置");
}

void TSM_Controller::beginPacing(const QVariantMap &params)
{
    const QString mode = params.value("pacingMode", "rate").toString();
    m_scheduler->setTcpMode(m_isTcpMode);
    if (mode == "interval")
        m_scheduler->setFixedIntervalMs(params.value("interval", 10).toInt());
    else
        m_scheduler->setRateMbps(params.value("targetMbps", 10.0).toDouble());
}

void TSM_Controller::startTest()
{
    if (m_active) {
        emit testStatusChanged(QStringLiteral("已有发送任务正在运行"));
        return;
    }
    if (m_currentConfig.isEmpty() || m_targets.isEmpty()) {
        emit testStatusChanged(QStringLiteral("请先完成目标配置"));
        return;
    }
    m_jobPacketSize = m_currentConfig.value("packetSize").toUInt();
    if (m_jobPacketSize < 1 || m_jobPacketSize > 65473) {
        emit testStatusChanged(QStringLiteral("数据包大小必须在 1 到 65473 字节之间"));
        return;
    }
    m_jobTotalPackets = m_currentConfig.value("packetCount").toUInt();
    if (m_jobTotalPackets == 0) return;
    m_jobBatch = ++m_nextBatch;
    m_batchTest = m_jobBatch;
    m_fileMode = false;
    m_isTcpMode = m_currentConfig.value("protocol").toString().compare("tcp", Qt::CaseInsensitive) == 0;
    m_jobDelayNs = m_delayInjector->calculateDelay();
    m_nextPacket = 0;
    m_sentPacketCount = 0;
    m_sentBytes = 0;
    m_activeProcessingNs = 0;
    m_generator->clearSentPackets();
    m_expectedPacketCount = quint64(m_jobTotalPackets) * m_targets.size();
    m_productionDone = false;
    m_sendFailure = false;
    m_jobClock.start();
    m_active = true;
    m_scheduler->clear();
    beginPacing(m_currentConfig);
    m_producerTimer->start();
    pumpPackets();
    emit testStatusChanged(QStringLiteral("测试发送中"));
}

void TSM_Controller::sendFile(const QVariantMap &params)
{
    if (m_active) {
        emit testStatusChanged(QStringLiteral("已有发送任务正在运行"));
        return;
    }
    if (m_targets.isEmpty()) {
        emit testStatusChanged(QStringLiteral("请先配置发送目标"));
        return;
    }
    m_jobPacketSize = params.value("packetSize").toUInt();
    if (m_jobPacketSize < 1 || m_jobPacketSize > 65473) {
        emit testStatusChanged(QStringLiteral("文件分片大小必须在 1 到 65473 字节之间"));
        return;
    }
    m_file.setFileName(params.value("filePath").toString());
    if (!m_file.open(QIODevice::ReadOnly)) {
        emit testStatusChanged(QStringLiteral("无法打开文件"));
        return;
    }
    m_fileSize = static_cast<quint64>(m_file.size());
    const quint64 count = 1 + m_fileSize / m_jobPacketSize + (m_fileSize % m_jobPacketSize != 0);
    if (count > std::numeric_limits<quint32>::max()) {
        m_file.close();
        emit testStatusChanged(QStringLiteral("文件过大，分片数超过协议上限"));
        return;
    }
    m_jobTotalPackets = static_cast<quint32>(count);
    m_fileName = QFileInfo(m_file.fileName()).fileName();
    m_jobBatch = ++m_nextBatch;
    m_batchFile = m_jobBatch;
    m_fileMode = true;
    m_isTcpMode = m_currentConfig.value("protocol").toString().compare("tcp", Qt::CaseInsensitive) == 0;
    m_jobDelayNs = m_delayInjector->calculateDelay();
    m_nextPacket = 0;
    m_sentPacketCount = 0;
    m_sentBytes = 0;
    m_activeProcessingNs = 0;
    m_generator->clearSentPackets();
    m_expectedPacketCount = quint64(m_jobTotalPackets) * m_targets.size();
    m_productionDone = false;
    m_sendFailure = false;
    m_jobClock.start();
    m_active = true;
    m_scheduler->clear();
    beginPacing(params);
    m_producerTimer->start();
    pumpPackets();
    emit testStatusChanged(QStringLiteral("文件发送中"));
}

void TSM_Controller::pumpPackets()
{
    if (!m_active || m_productionDone) return;
    // Bound producer memory even for a large file or a very low target rate.
    int produced = 0;
    while (m_nextPacket < m_jobTotalPackets &&
           m_scheduler->queuedPackets() + m_targets.size() <= qMax(256, m_targets.size()) && produced < 128) {
        QElapsedTimer packetWork;
        packetWork.start();
        const quint8 priority = m_scheduler->generatePriority();
        QByteArray packet;
        if (m_fileMode) {
            bool last = false;
            packet = m_generator->generateFilePacket(m_file, m_jobPacketSize, last,
                m_nextPacket == 0, m_fileName, m_fileSize, priority,
                m_jobTotalPackets, m_jobBatch);
        } else {
            packet = m_generator->generatePacket(m_jobPacketSize, priority,
                                                  m_jobTotalPackets, m_jobBatch);
        }
        if (packet.isEmpty()) {
            stopTest();
            emit testStatusChanged(QStringLiteral("生成数据包失败，任务已停止"));
            return;
        }
        for (const auto &target : m_targets)
            m_scheduler->schedulePacket(packet, target.first, target.second,
                                        m_jobDelayNs, priority);
        m_activeProcessingNs += packetWork.nsecsElapsed();
        ++m_nextPacket;
        ++produced;
    }
    if (m_nextPacket == m_jobTotalPackets) {
        m_productionDone = true;
        m_producerTimer->stop();
        if (m_file.isOpen()) m_file.close();
    }
}

void TSM_Controller::onPacketDispatched()
{
    if (!m_active) return;
    if (m_productionDone && m_sentPacketCount == m_expectedPacketCount) {
        const double elapsedMs = m_jobClock.nsecsElapsed() / 1000000.0;
        const double processingMs = m_activeProcessingNs / 1000000.0;
        if (m_fileMode) m_fileTotalDuration = processingMs;
        else if (m_isTcpMode) m_testTotalMs = processingMs;
        else m_totalDuration = processingMs;
        m_active = false;
        m_producerTimer->stop();
        emit packetSent(m_generator->getSentPackets());
        const double actualMbps = elapsedMs > 0 ? m_sentBytes * 8.0 / (elapsedMs * 1000.0) : 0.0;
        emit testStatusChanged(QStringLiteral("%1发送完成，%2 次发送，入队耗时 %3 ms，平均入队速率 %4 Mbps")
            .arg(m_fileMode ? QStringLiteral("文件") : QStringLiteral("测试"))
            .arg(m_sentPacketCount).arg(elapsedMs, 0, 'f', 2).arg(actualMbps, 0, 'f', 3));
    }
}

void TSM_Controller::stopTest()
{
    m_active = false;
    m_producerTimer->stop();
    m_scheduler->clear();
    if (m_file.isOpen()) m_file.close();
    emit packetSent(m_generator->getSentPackets());
    emit testStatusChanged(QStringLiteral("测试已停止"));
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

    const double aDuration = batchNumber == m_batchFile ? m_fileTotalDuration : m_totalDuration;

    emit testStatusChanged(
        QString("批次 %1 总反馈：A端处理时间 %2 ms, B端处理时间 %3 ms，C端处理时间 %4 ms")
            .arg(batchNumber)
            .arg(aDuration, 0, 'f', 2)
            .arg(bDurationMs, 0, 'f', 2)
            .arg(cDurationMs, 0, 'f', 2));
    // 新增：发送UDP时延统计数据
    emit delayStatsUpdatedUDP(aDuration, bDurationMs, cDurationMs);
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
