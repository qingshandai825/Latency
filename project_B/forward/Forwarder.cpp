#include "forwarder.h"
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>

Forwarder::Forwarder(QObject *parent)
    : QObject(parent),
    senderServer_(new QTcpServer(this)),
    senderSocket_(nullptr),
    receiverSocket_(new QTcpSocket(this)),
    senderPacketCount_(0),
    forwardPacketCount_(0),
    recvIp_(),
    recvPort_(0),
    consecutiveElapsedAbnormal_(0)  // 初始化计数器
{
    connect(senderServer_, &QTcpServer::newConnection, this, &Forwarder::onSenderNewConnection);

    connect(receiverSocket_, &QTcpSocket::connected, this, &Forwarder::onReceiverConnected);
    connect(receiverSocket_, &QTcpSocket::disconnected, this, &Forwarder::onReceiverDisconnected);
    connect(receiverSocket_, &QTcpSocket::readyRead, this, &Forwarder::onReceiverReadyRead);
    connect(receiverSocket_,&QTcpSocket::errorOccurred,
            this, &Forwarder::onReceiverError);
}

Forwarder::~Forwarder()
{
    stop();
    disconnect(senderServer_, nullptr, this, nullptr);
    disconnect(receiverSocket_, nullptr, this, nullptr);
    if (senderSocket_) {
        disconnect(senderSocket_, nullptr, this, nullptr);
    }
}

void Forwarder::start(int senderListenPort, const QString &recvIp, int recvPort)
{
    recvIp_ = recvIp;
    recvPort_ = recvPort;

    if (!senderServer_->listen(QHostAddress::Any, senderListenPort)) {
        emit statusUpdated(QString("❌ 监听发送端端口 %1 失败").arg(senderListenPort));
        return;
    }
    emit statusUpdated(QString("✅ 监听发送端端口 %1 成功").arg(senderListenPort));

    if (receiverSocket_->state() != QAbstractSocket::ConnectedState) {
        receiverSocket_->connectToHost(recvIp_, recvPort_);
        emit statusUpdated(QString("▶️ 连接接收端 %1:%2").arg(recvIp_).arg(recvPort_));
    }

    senderPacketCount_ = 0;
    forwardPacketCount_ = 0;
    emit senderPacketCountChanged(senderPacketCount_);
    emit forwardPacketCountChanged(forwardPacketCount_);
}

void Forwarder::stop()
{
    if (senderServer_->isListening()) {
        senderServer_->close();
    }

    if (senderSocket_) {
        senderSocket_->disconnectFromHost();
        if (senderSocket_->state() != QAbstractSocket::UnconnectedState) {
            senderSocket_->waitForDisconnected(100);
        }
        disconnect(senderSocket_, nullptr, this, nullptr);
        senderSocket_->deleteLater();
        senderSocket_ = nullptr;
    }

    if (receiverSocket_->state() != QAbstractSocket::UnconnectedState) {
        receiverSocket_->disconnectFromHost();
        receiverSocket_->waitForDisconnected(100);
        disconnect(receiverSocket_, nullptr, this, nullptr);
    }

    senderPacketCount_ = 0;
    forwardPacketCount_ = 0;
    emit senderPacketCountChanged(senderPacketCount_);
    emit forwardPacketCountChanged(forwardPacketCount_);
    emit statusUpdated("⏹️ 转发模块已停止");
    emit receiverConnectionStatusChanged(false);

    // 关键：不重置 consecutiveElapsedAbnormal_，保留历史计数
}

bool Forwarder::isReceiverConnected() const
{
    return receiverSocket_ && receiverSocket_->state() == QAbstractSocket::ConnectedState;
}

void Forwarder::onSenderNewConnection()
{
    if (senderSocket_) {
        // 只允许一个发送端连接，断开旧连接
        senderSocket_->disconnect();
        senderSocket_->close();
        senderSocket_->deleteLater();
        senderSocket_ = nullptr;
    }

    senderSocket_ = senderServer_->nextPendingConnection();
    connect(senderSocket_, &QTcpSocket::readyRead, this, &Forwarder::onSenderReadyRead);
    connect(senderSocket_, &QTcpSocket::disconnected, [this]() {
        emit statusUpdated("❌ 发送端连接断开");
        senderSocket_->deleteLater();
        senderSocket_ = nullptr;
    });

    emit statusUpdated("🔗 发送端已连接");
}

void Forwarder::onSenderReadyRead()
{
    if (!senderSocket_ || !receiverSocket_ || receiverSocket_->state() != QAbstractSocket::ConnectedState)
        return;
    dataTransmitTimer.start();

    QByteArray data = senderSocket_->readAll();
    senderPacketCount_ += 1;
    emit senderPacketCountChanged(senderPacketCount_);

    // 转发给接收端
    receiverSocket_->write(data);
    forwardPacketCount_ += 1;
    emit forwardPacketCountChanged(forwardPacketCount_);

    emit statusUpdated(QString("📤 收到发送端数据，已转发给接收端，包大小 %1 字节").arg(data.size()));
}

void Forwarder::onReceiverConnected()
{
    emit statusUpdated(QString("✅ 成功连接接收端 %1:%2").arg(recvIp_).arg(recvPort_));
    emit receiverConnectionStatusChanged(true);
}

void Forwarder::onReceiverDisconnected()
{
    emit statusUpdated("⚠️ 接收端连接断开，尝试重连...");
    emit receiverConnectionStatusChanged(false);

    // 1秒后自动重连
    QTimer::singleShot(1000, this, [this]() {
        if (receiverSocket_ && receiverSocket_->state() == QAbstractSocket::UnconnectedState) {
            receiverSocket_->connectToHost(recvIp_, recvPort_);
            emit statusUpdated(QString("▶️ 尝试重新连接接收端 %1:%2").arg(recvIp_).arg(recvPort_));
        }
    });
}

void Forwarder::onReceiverReadyRead()
{
    if (!senderSocket_ || senderSocket_->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray data = receiverSocket_->readAll();
    const int timeDataSize = sizeof(qint64);

    qint64 receiverElapsed = -1;
    if (data.size() >= timeDataSize) {
        QByteArray timeData = data.right(timeDataSize);
        QDataStream in(&timeData, QIODevice::ReadOnly);
        in.setByteOrder(QDataStream::BigEndian);
        in >> receiverElapsed;
    }

    // ---------------- 高精度耗时计算（核心修改） ----------------
    // 计算从发送到接收响应的耗时（纳秒），转换为毫秒（保留小数）
    qint64 elapsedNs = dataTransmitTimer.nsecsElapsed();  // 纳秒级耗时
    double elapsedMs = elapsedNs / 1000000.0;  // 转换为毫秒（1毫秒 = 1e6纳秒）
    // ------------------------------------------------------------

    // ---------------- 时延阈值判断逻辑（更新为double比较） ----------------
    if (elapsedMs > elapsedThreshold_) {  // elapsedThreshold_ 建议也改为double类型
        consecutiveElapsedAbnormal_++;
        if (consecutiveElapsedAbnormal_ >= abnormalCountThreshold_) {
            // 显示时保留两位小数
            QString alarmMsg = QString("🚨 中间模块时延异常！当前耗时: %1 ms，连续异常: %2 次")
                                   .arg(elapsedMs, 0, 'f', 2)  // 格式化小数
                                   .arg(consecutiveElapsedAbnormal_);
            emit elapsedAlarm(alarmMsg);
            emit statusUpdated(alarmMsg);
        }
    } else {
        if (consecutiveElapsedAbnormal_ > 0) {
            QString recoverMsg = QString("✅ 中间模块时延恢复正常: %1 ms")
                                     .arg(elapsedMs, 0, 'f', 2);  // 格式化小数
            emit elapsedRecovered(recoverMsg);
            emit statusUpdated(recoverMsg);
            consecutiveElapsedAbnormal_ = 0;
        }
    }
    // ----------------------------------------------------------------------

    QByteArray newData = data;

    // ---------------- 转发耗时数据（改为double类型） ----------------
    QByteArray forwarderTimeData;
    QDataStream forwarderOut(&forwarderTimeData, QIODevice::WriteOnly);
    forwarderOut.setByteOrder(QDataStream::BigEndian);
    forwarderOut << elapsedMs;  // 写入double类型的耗时
    newData.append(forwarderTimeData);
    // ----------------------------------------------------------------

    QByteArray receiverTimeDataOut;
    QDataStream receiverOut(&receiverTimeDataOut, QIODevice::WriteOnly);
    receiverOut.setByteOrder(QDataStream::BigEndian);
    receiverOut << receiverElapsed;
    newData.append(receiverTimeDataOut);

    senderSocket_->write(newData);

    // ---------------- 日志输出（显示小数） ----------------
    QString logMsg;
    if (receiverElapsed != -1) {
        logMsg = QString("转发数据，中间耗时：%1 ms，接收端耗时：%2 ms")
                     .arg(elapsedMs, 0, 'f', 2)  // 保留两位小数
                     .arg(static_cast<double>(receiverElapsed), 0, 'f', 2);  // 接收端耗时也转为小数
    } else {
        logMsg = QString("转发数据，中间耗时：%1 ms，接收端耗时：未解析")
                     .arg(elapsedMs, 0, 'f', 2);
    }
    qDebug() << logMsg;
    emit statusUpdated(logMsg);
    // ------------------------------------------------------
}

void Forwarder::onReceiverError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)
    emit statusUpdated(QString("⚠️ 接收端连接错误: %1").arg(receiverSocket_->errorString()));
}
