#include "background_sender.h"
#include <QDebug>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>

BackgroundSender::BackgroundSender(QObject *parent)
    : QObject(parent),
    timer_(nullptr),
    socket_(nullptr),
    port_(0),
    pktSize_(0),
    packetsPerInterval_(0),
    totalBytes_(0),
    stopped_(true)
{
}

void BackgroundSender::start(const QString &ip, quint16 port, int pktSize, int mbps)
{
    if (!stopped_) {
        qDebug() << "⚠️ 背景流量已启动";
        return;
    }

    if (pktSize <= 0 || port == 0 || mbps <= 0) {
        qDebug() << "❌ 参数错误";
        return;
    }

    ip_ = ip;
    port_ = port;
    pktSize_ = pktSize;
    stopped_ = false;
    totalBytes_ = 0;
    lastSec_ = QDateTime::currentMSecsSinceEpoch();

    int intervalMs = 100;
    double bytesPerSec = mbps * 1024.0 * 1024.0;
    int bytesPerInterval = bytesPerSec * intervalMs / 1000.0;
    packetsPerInterval_ = bytesPerInterval / pktSize_;
    if (packetsPerInterval_ < 1) packetsPerInterval_ = 1;

    qDebug() << "📊 每" << intervalMs << "ms预计发送"
             << packetsPerInterval_ << "个包，总字节"
             << packetsPerInterval_ * pktSize_;

    if (!socket_) {
        socket_ = new QUdpSocket(this);
    }
    if (!timer_) {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &BackgroundSender::onTimeout);
    }

    timer_->start(intervalMs);
    qDebug() << "🟢 背景流量启动：" << ip_ << port_ << pktSize_ << mbps << "MBps";
}

void BackgroundSender::stop()
{
    if (stopped_) return;

    stopped_ = true;
    if (timer_ && timer_->isActive()) timer_->stop();
    if (socket_) socket_->close();

    totalBytes_ = 0;
    lastSec_ = QDateTime::currentMSecsSinceEpoch();

    qDebug() << "🛑 背景流量已停止";
}

void BackgroundSender::onTimeout()
{
    if (stopped_ || !socket_) return;

    QByteArray payload(pktSize_, 'X');
    int sentCount = 0;

    QElapsedTimer loopTimer;
    loopTimer.start();

    // 发送 packetsPerInterval_ 个数据包
    for (int i = 0; i < packetsPerInterval_; ++i) {
        qint64 bytes = socket_->writeDatagram(payload, QHostAddress(ip_), port_);
        if (bytes > 0) {
            sentCount++;
            totalBytes_ += bytes;
        }
        // 控制单个包之间间隔（微调）
        QThread::usleep(5);
    }

    // 打印本次发送包数及耗时
    qDebug() << "📤 发送" << sentCount << "包，用时" << loopTimer.elapsed() << "ms";

    // 每秒统计实际速率
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - lastSec_;
    if (elapsed >= 1000) {
        double mbpsReal = totalBytes_ / 1024.0 / 1024.0 * (1000.0 / elapsed); // 考虑实际间隔
        qDebug() << "📈 实际发送速率:" << QString::number(mbpsReal, 'f', 2) << "MBps";
        totalBytes_ = 0;
        lastSec_ = now;
    }
}
