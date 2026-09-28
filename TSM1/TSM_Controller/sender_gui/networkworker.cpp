#include "networkworker.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QHostAddress>
#include <QDataStream>
#include <QDebug>

// ---- 构造/析构 ----
NetworkWorker::NetworkWorker(QObject *parent)
    : QObject(parent)
{}

NetworkWorker::~NetworkWorker() {
    stopTest();
}

// ---- 启动/停止 ----
void NetworkWorker::startTest(const QString &ip, int port, int pktSize, int bgMbps, bool useUDP) {
    // 保存配置
    useUDP_   = useUDP;
    ip_       = ip;
    port_     = port;
    pktSize_  = pktSize > 0 ? pktSize : 64;
    bgMbps_   = bgMbps;
    probeCount_ = 0;
    consecutiveAbnormalCount_ = 0;

    // 清理旧对象（防重复启动）
    stopTest();

    // 建链
    if (useUDP_) {
        udp_ = new QUdpSocket(this);
        connect(udp_, &QUdpSocket::readyRead, this, &NetworkWorker::onReadyRead, Qt::UniqueConnection);
        emit statusUpdate(QStringLiteral("✅ UDP 就绪：%1:%2").arg(ip_).arg(port_));
    } else {
        tcp_ = new QTcpSocket(this);
        connect(tcp_, &QTcpSocket::readyRead, this, &NetworkWorker::onReadyRead, Qt::UniqueConnection);
        tcp_->connectToHost(ip_, port_);
        if (!tcp_->waitForConnected(1500)) {
            emit statusUpdate(QStringLiteral("❌ TCP 连接失败：%1").arg(tcp_->errorString()));
            // 仍允许后续重试或用户手动停止
        } else {
            emit statusUpdate(QStringLiteral("✅ TCP 已连接：%1:%2").arg(ip_).arg(port_));
        }
    }

    // 背景吞吐发生器（可选）
    if (bgMbps_ > 0) {
        bgProcess_ = new QProcess(this);
        QString program = QCoreApplication::applicationDirPath() + "/background_sender";
#ifdef Q_OS_WIN
        program += ".exe";
#endif
        QStringList args;
        args << ip_ << QString::number(port_) << QString::number(pktSize_) << QString::number(bgMbps_);
        bgProcess_->start(program, args);
        if (bgProcess_->waitForStarted(1000)) {
            emit statusUpdate(QStringLiteral("✅ background_sender 启动成功（%1 MBps）").arg(bgMbps_));
        } else {
            emit statusUpdate(QStringLiteral("❌ 启动 background_sender 失败：") + bgProcess_->errorString());
        }
    }

    // 定时探测
    probeTimer_ = new QTimer(this);
    probeTimer_->setTimerType(Qt::PreciseTimer);
    probeTimer_->setInterval(1000);  // 1s 一个探测
    connect(probeTimer_, &QTimer::timeout, this, &NetworkWorker::onProbeTick, Qt::UniqueConnection);
    probeTimer_->start();

    emit statusUpdate(QStringLiteral("测试已启动"));
}

void NetworkWorker::stopTest() {
    if (probeTimer_) { probeTimer_->stop(); probeTimer_->deleteLater(); probeTimer_ = nullptr; }
    if (udp_)        { udp_->deleteLater(); udp_ = nullptr; }
    if (tcp_)        {
        if (tcp_->state() != QAbstractSocket::UnconnectedState) tcp_->disconnectFromHost();
        tcp_->deleteLater(); tcp_ = nullptr;
    }
    if (bgProcess_)  { bgProcess_->kill();  bgProcess_->deleteLater(); bgProcess_ = nullptr; }
}

// ---- 发送探测 ----
void NetworkWorker::sendProbe() {
    // 构造载荷：前8字节写入当前毫秒时间戳，剩余填充
    QByteArray payload(pktSize_, 0);
    const qint64 t0ms = QDateTime::currentMSecsSinceEpoch();
    static_assert(sizeof(qint64) == 8, "qint64 size must be 8");
    memcpy(payload.data(), &t0ms, sizeof(qint64));

    probePayload_ = payload.left(sizeof(qint64)); // 仅比对前8字节即可
    rttClock_.restart();                          // 发送瞬间重启计时
    ++probeCount_;

    if (useUDP_) {
        if (udp_) udp_->writeDatagram(payload, QHostAddress(ip_), port_);
    } else {
        if (tcp_ && tcp_->state() == QAbstractSocket::ConnectedState) {
            tcp_->write(payload);
            tcp_->flush();
        }
    }
}

// 定时触发探测
void NetworkWorker::onProbeTick() {
    sendProbe();
}

// ---- 接收回包并计算 RTT ----
void NetworkWorker::onReadyRead() {
    if (useUDP_) {
        if (!udp_) return;
        while (udp_->hasPendingDatagrams()) {
            QByteArray buf;
            buf.resize(udp_->pendingDatagramSize());
            udp_->readDatagram(buf.data(), buf.size());

            // 匹配本次探测
            if (buf.size() >= int(sizeof(qint64)) &&
                buf.left(sizeof(qint64)) == probePayload_) {
                const qint64 elapsedNs = rttClock_.nsecsElapsed();
                const double rttMs = elapsedNs / 1e4;  // ns -> ms
                processRttAndNotify(rttMs, 0.0, 0.0);  // UDP 模式 B/C 不统计
                // 收到匹配就返回（防止一次 readRead 处理多包干扰“每3次”的节拍）
                return;
            }
        }
    } else {
        if (!tcp_) return;
        // 注意：这里用 readAll() 简化处理，真实场景应做帧边界解析
        QByteArray buf = tcp_->readAll();
        if (buf.size() < int(sizeof(qint64))) return;

        if (buf.left(sizeof(qint64)) != probePayload_) {
            emit statusUpdate(QStringLiteral("❌ TCP 回传首部不匹配，忽略"));
            return;
        }

        // 末尾两个时间：B段(double)、C段(qint64) —— 保持你原协议读法
        double bMs = 0.0;
        double cMs = 0.0;
        if (buf.size() >= int(sizeof(qint64) + sizeof(double) + sizeof(qint64))) {
            QByteArray bBytes = buf.mid(buf.size() - int(sizeof(double) + sizeof(qint64)), sizeof(double));
            QByteArray cBytes = buf.right(sizeof(qint64));

            QDataStream bIn(bBytes);
            bIn.setByteOrder(QDataStream::BigEndian);
            bIn >> bMs;

            qint64 cVal = 0;
            QDataStream cIn(cBytes);
            cIn.setByteOrder(QDataStream::BigEndian);
            cIn >> cVal;
            cMs = static_cast<double>(cVal);
        }

        const qint64 elapsedNs = rttClock_.nsecsElapsed();
        const double aMs = elapsedNs / 1e4;  // A 段 RTT(ms)
        processRttAndNotify(aMs, bMs, cMs);
    }
}

// ---- 统一后处理：模拟重传 + 告警 + 发信号 ----
void NetworkWorker::processRttAndNotify(double aMs, double bMs, double cMs) {
    bool isRetrans = false;
    double aShown = aMs;

    if (simulateRetransmission_ && (probeCount_ % 3 == 0)) {
        aShown += 50.0;     // 每第3次人为+50ms
        isRetrans = true;
    }

    // 状态文本（TCP 显示 A/B/C；UDP 只显示 A）
    if (useUDP_) {
        emit statusUpdate(QStringLiteral("瞬时RTT = %1 ms%2")
                              .arg(aShown, 0, 'f', 2)
                              .arg(isRetrans ? QStringLiteral("（模拟重传+50ms）") : QString()));
    } else {
        emit statusUpdate(QStringLiteral("A=%1ms, B=%2ms, C=%3ms%4")
                              .arg(aShown, 0, 'f', 2)
                              .arg(bMs,    0, 'f', 2)
                              .arg(cMs,    0, 'f', 2)
                              .arg(isRetrans ? QStringLiteral("（模拟重传+50ms）") : QString()));
    }

    // 图/统计
    emit rttMeasured(aShown, bgMbps_, isRetrans);
    emit delayStatsUpdated(aShown, bMs, cMs);

    // 告警判定只看 A 段（显示值）
    if (aShown/100 > abnormalThreshold_) {
        consecutiveAbnormalCount_++;
        if (consecutiveAbnormalCount_ >= thresholdCount_) {
            emit rttAlarm(QStringLiteral("🚨 RTT 异常：%1 ms，阈值：%2 ms，连续：%3 次")
                              .arg(aShown/100, 0, 'f', 2)
                              .arg(abnormalThreshold_)
                              .arg(consecutiveAbnormalCount_));
        }
    } else {
        if (consecutiveAbnormalCount_ > 0) {
            emit rttRecovered(QStringLiteral("✅ RTT 恢复：%1 ms").arg(aShown, 0, 'f', 2));
            consecutiveAbnormalCount_ = 0;
        }
    }
}








