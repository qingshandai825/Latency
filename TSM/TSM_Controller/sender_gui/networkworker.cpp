#include "networkworker.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include <QTimer>

NetworkWorker::NetworkWorker(QObject *parent)
    : QObject(parent),
    useUDP_(true),
    port_(0),
    pktSize_(0),
    bgMbps_(0),
    tcp_(nullptr),
    udp_(nullptr),
    probeTimer_(nullptr),
    lastProbeNSec_(0),
    bgProcess_(nullptr),
    simulateRetransmission_(false),
    probeCount_(0),
    consecutiveAbnormalCount_(0)
{}

NetworkWorker::~NetworkWorker() {
    stopTest();
}

void NetworkWorker::setSimulateRetransmission(bool simulate) {
    simulateRetransmission_ = simulate;
}

void NetworkWorker::startTest(const QString &ip, int port, int pktSize, int bgMbps, bool useUDP) {
    useUDP_ = useUDP;
    ip_ = ip;
    port_ = port;
    pktSize_ = pktSize;
    bgMbps_ = bgMbps;
    consecutiveAbnormalCount_ = 0;

    if (useUDP_) {
        udp_ = new QUdpSocket(this);
        connect(udp_, &QUdpSocket::readyRead, this, &NetworkWorker::handleProbeReply, Qt::UniqueConnection);
    } else {
        tcp_ = new QTcpSocket(this);
        tcp_->connectToHost(ip_, port_);
        if (!tcp_->waitForConnected(1000)) {
            emit statusUpdate("❌ TCP 连接失败");
            return;
        }
        connect(tcp_, &QTcpSocket::readyRead, this, &NetworkWorker::handleProbeReply);
    }

    probeTimer_ = new QTimer(this);
    probeTimer_->setInterval(1000);
    connect(probeTimer_, &QTimer::timeout, this, &NetworkWorker::sendProbe);
    probeTimer_->start();

    if (bgMbps_ > 0) {
        bgProcess_ = new QProcess(this);
        QString program = QCoreApplication::applicationDirPath() + "/background_sender";
#ifdef Q_OS_WIN
        program += ".exe";
#endif
        QStringList args;
        args << ip_ << QString::number(port_)
             << QString::number(pktSize_)
             << QString::number(bgMbps_);
        bgProcess_->start(program, args);
        if (bgProcess_->waitForStarted(1000)) {
            emit statusUpdate("✅ background_sender 启动成功");
            qDebug() << "✅ background_sender args:" << args;
        } else {
            emit statusUpdate("❌ 启动 background_sender 失败");
            qDebug() << "❌ background_sender error:" << bgProcess_->errorString();
        }
    }

    emit statusUpdate("测试已启动");
}

void NetworkWorker::stopTest() {
    if (probeTimer_) {
        probeTimer_->stop();
        probeTimer_->deleteLater();
        probeTimer_ = nullptr;
    }
    if (udp_) {
        udp_->deleteLater();
        udp_ = nullptr;
    }
    if (tcp_) {
        tcp_->disconnectFromHost();
        tcp_->deleteLater();
        tcp_ = nullptr;
    }
    if (bgProcess_) {
        bgProcess_->kill();
        bgProcess_->deleteLater();
        bgProcess_ = nullptr;
    }
}

void NetworkWorker::sendProbe() {
    QByteArray payload(pktSize_, 0);
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    memcpy(payload.data(), &now, sizeof(qint64));

    probePayload_ = payload;
    timer_.start();
    lastProbeNSec_ = timer_.nsecsElapsed();
    ++probeCount_;

    if (useUDP_) {
        udp_->writeDatagram(payload, QHostAddress(ip_), port_);
        qDebug() << "📤 已发送 UDP 探针包，等待回包...";
    } else {
        if (tcp_ && tcp_->state() == QAbstractSocket::ConnectedState) {
            tcp_->write(payload);
        }
    }
}

void NetworkWorker::handleProbeReply() {
    QByteArray buf;
    double rtt = 0.0;

    if (useUDP_) {
        while (udp_->hasPendingDatagrams()) {
            buf.resize(udp_->pendingDatagramSize());
            udp_->readDatagram(buf.data(), buf.size());
            if (buf.left(sizeof(qint64)) == probePayload_.left(sizeof(qint64))) {
                qint64 elapsedNs = timer_.nsecsElapsed() - lastProbeNSec_;
                rtt = elapsedNs / 10000.0;
                double rttMs = rtt / 100.0;
                emit rttMeasured(rtt, bgMbps_, false);
                emit statusUpdate(QString("瞬时RTT = %1 ms").arg(rttMs, 0, 'f', 2));

                // 发出统计信号（UDP 模式下 B、C 端时延未知，设为 0）
                emit delayStatsUpdated(rttMs, 0.0, 0.0);

                if (rttMs > abnormalThreshold_) {
                    consecutiveAbnormalCount_++;
                    if (consecutiveAbnormalCount_ >= thresholdCount_) {
                        QString alarmMsg = QString("🚨 UDP RTT 异常告警！当前RTT: %1 ms，阈值: %2 ms，连续异常: %3 次")
                                               .arg(rttMs, 0, 'f', 2)
                                               .arg(abnormalThreshold_)
                                               .arg(consecutiveAbnormalCount_);
                        emit rttAlarm(alarmMsg);
                    }
                } else {
                    if (consecutiveAbnormalCount_ > 0) {
                        QString recoverMsg = QString("✅ UDP RTT 恢复正常: %1 ms").arg(rttMs, 0, 'f', 2);
                        emit rttRecovered(recoverMsg);
                        consecutiveAbnormalCount_ = 0;
                    }
                }
                return;
            }
        }
    } else {
        if (!tcp_ || tcp_->bytesAvailable() < (pktSize_ + 2 * sizeof(qint64))) {
            return;
        }

        buf = tcp_->readAll();
        if (buf.left(sizeof(qint64)) != probePayload_.left(sizeof(qint64))) {
            emit statusUpdate("❌ TCP回传数据不匹配，忽略");
            return;
        }

        QByteArray originalData = buf.left(buf.size() - 2 * sizeof(qint64));
        QByteArray forwarderTimeData = buf.mid(buf.size() - 2 * sizeof(qint64), sizeof(qint64));
        double forwarderElapsed;
        QDataStream forwarderIn(&forwarderTimeData, QIODevice::ReadOnly);
        forwarderIn.setByteOrder(QDataStream::BigEndian);
        forwarderIn >> forwarderElapsed;

        QByteArray receiverTimeData = buf.right(sizeof(qint64));
        qint64 receiverElapsed;
        QDataStream receiverIn(&receiverTimeData, QIODevice::ReadOnly);
        receiverIn.setByteOrder(QDataStream::BigEndian);
        receiverIn >> receiverElapsed;

        qint64 elapsedNs = timer_.nsecsElapsed() - lastProbeNSec_;
        rtt = elapsedNs / 10000.0;
        double rttMs = rtt / 100.0;
        QString statusMsg = QString("A处理时间=%1ms，B处理时间=%2ms，C处理时间=%3ms")
                                .arg(rttMs, 0, 'f', 2)
                                .arg(forwarderElapsed, 0, 'f', 2)
                                .arg(receiverElapsed);
        emit statusUpdate(statusMsg);
        qDebug() << statusMsg;
        emit rttMeasured(rtt, bgMbps_, false);

        // 发出统计信号
        emit delayStatsUpdated(rttMs, static_cast<double>(forwarderElapsed), static_cast<double>(receiverElapsed));

        if (rttMs > abnormalThreshold_) {
            consecutiveAbnormalCount_++;
            if (consecutiveAbnormalCount_ >= thresholdCount_) {
                QString alarmMsg = QString("🚨 TCP RTT 异常告警！当前RTT: %1 ms，连续异常: %2 次")
                                       .arg(rttMs, 0, 'f', 2)
                                       .arg(consecutiveAbnormalCount_);
                emit rttAlarm(alarmMsg);
            }
        } else {
            if (consecutiveAbnormalCount_ > 0) {
                QString recoverMsg = QString("✅ TCP RTT 恢复正常: %1 ms").arg(rttMs, 0, 'f', 2);
                emit rttRecovered(recoverMsg);
                consecutiveAbnormalCount_ = 0;
            }
        }
    }
}
