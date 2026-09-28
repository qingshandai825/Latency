#include "receiverwindow.h"
#include "ui_receiverwindow.h"
#include <QRandomGenerator>
#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>

ReceiverWindowLi::ReceiverWindowLi(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::ReceiverWindowLi),
    server_(nullptr),
    client_(nullptr),
    lossRate_(30),
    consecutiveElapsedAbnormal_(0) {  // 初始化计数器
    ui->setupUi(this);
    connect(ui->startButton, &QPushButton::clicked, this, &ReceiverWindowLi::onStartClicked);

    // 基础控件设置
    ui->portSpin->setRange(1, 65535);
    ui->portSpin->setValue(8081);
    ui->lossSpin->setRange(0, 100);
    ui->lossSpin->setValue(30);

    // 新增：添加时延阈值设置控件（需在UI设计师中添加QSpinBox，对象名：elapsedThresholdSpin）
    ui->elapsedThresholdSpin->setRange(0, 2000);  // 范围1-2000ms
    ui->elapsedThresholdSpin->setValue(1000);       // 默认阈值1000ms
    // 新增：连续异常次数设置（需在UI设计师中添加QSpinBox，对象名：abnormalCountSpin）
    ui->abnormalCountSpin->setRange(1, 10);        // 范围1-10次
    ui->abnormalCountSpin->setValue(3);            // 默认3次

    // 初始化告警标签（需在UI设计师中添加QLabel，对象名：alarmLabel）
    ui->alarmLabel->setText("处理时延状态：正常");
    ui->alarmLabel->setStyleSheet("color: green;");
}

ReceiverWindowLi::~ReceiverWindowLi() {
    if (client_) {
        client_->disconnectFromHost();
        client_->deleteLater();
    }
    if (server_) {
        server_->close();
        server_->deleteLater();
    }
    delete ui;
}

void ReceiverWindowLi::onStartClicked() {
    if (server_) return;
    int port = ui->portSpin->value();
    lossRate_ = ui->lossSpin->value();

    // 启动时同步阈值设置
    elapsedThreshold_ = ui->elapsedThresholdSpin->value();
    abnormalCountThreshold_ = ui->abnormalCountSpin->value();

    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &ReceiverWindowLi::onNewConnection);
    if (!server_->listen(QHostAddress::Any, port)) {
        ui->statusLabel->setText("❌ 无法监听端口");
    } else {
        ui->statusLabel->setText(QString("✅ 正在监听端口 %1").arg(port));
    }
}

void ReceiverWindowLi::onNewConnection() {
    if (client_) client_->deleteLater();
    client_ = server_->nextPendingConnection();
    connect(client_, &QTcpSocket::readyRead, this, &ReceiverWindowLi::onDataReceived);
    connect(client_, &QTcpSocket::disconnected, [this]() {
        ui->statusLabel->setText("❌ 客户端已断开");
        client_->deleteLater();
        client_ = nullptr;
    });
    ui->statusLabel->setText("🔗 客户端已连接");
}

void ReceiverWindowLi::onDataReceived() {
    if (!client_) return;

    QElapsedTimer timer;
    timer.start();

    QByteArray data = client_->readAll();

    // 处理耗时（模拟实际处理）
    qint64 elapsedMs = 0;
    {
        // 模拟处理（可根据需要调整耗时逻辑）
        while (timer.elapsed() < 1) {}  // 至少1ms
        elapsedMs = timer.elapsed();
        qDebug() << "📦 数据处理耗时:" << elapsedMs << "ms";
    }

    // ---------------- 新增：时延阈值判断逻辑 ----------------
    // 实时同步阈值（支持动态修改）
    elapsedThreshold_ = ui->elapsedThresholdSpin->value();
    abnormalCountThreshold_ = ui->abnormalCountSpin->value();

    if (elapsedMs > elapsedThreshold_) {
        consecutiveElapsedAbnormal_++;  // 超过阈值，计数器递增
        // 达到连续异常次数时触发告警
        if (consecutiveElapsedAbnormal_ >= abnormalCountThreshold_) {
            QString alarmMsg = QString("🚨 处理时延异常！当前：%1 ms，连续异常：%2次")
                                   .arg(elapsedMs).arg(consecutiveElapsedAbnormal_);
            ui->alarmLabel->setText(alarmMsg);
            ui->alarmLabel->setStyleSheet("color: red; font-weight: bold;");
            // 可同时发送信号给其他模块：emit elapsedAlarm(alarmMsg);
        }
    } else {
        // 恢复正常时重置计数器
        if (consecutiveElapsedAbnormal_ > 0) {
            QString normalMsg = QString("✅ 处理时延恢复正常：%1 ms（阈值：%2 ms）")
                                    .arg(elapsedMs).arg(elapsedThreshold_);
            ui->alarmLabel->setText(normalMsg);
            ui->alarmLabel->setStyleSheet("color: green;");
            consecutiveElapsedAbnormal_ = 0;
            // 可同时发送信号：emit elapsedRecovered(normalMsg);
        } else {
            // 持续正常状态，更新显示
            ui->alarmLabel->setText(QString("处理时延状态：正常（%1 ms）").arg(elapsedMs));
        }
    }
    // ---------------------------------------------------------

    // 构造带反馈时间的数据包（原逻辑保留）
    QByteArray reply = data;
    QDataStream out(&reply, QIODevice::Append);
    out.setByteOrder(QDataStream::BigEndian);
    out << qint64(elapsedMs);  // 添加8字节处理时延

    client_->write(reply);
}
