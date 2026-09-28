#include "forwarderwindow.h"
#include "ui_forwarderwindow.h"
#include <QIntValidator>
#include <QHostAddress>

ForwarderWindow::ForwarderWindow(QWidget *parent)
    : QWidget(parent),
    ui(new Ui::ForwarderWindow),
    forwarder_(new Forwarder(this))
{
    ui->setupUi(this);

    ui->senderPortEdit->setValidator(new QIntValidator(1, 65535, this));
    ui->receiverIpEdit->setText("127.0.0.1");
    ui->receiverPortEdit->setValidator(new QIntValidator(1, 65535, this));

    ui->senderPortEdit->setText("8080");
    ui->receiverPortEdit->setText("8081");
    // 新增：时延阈值输入框（范围1-10000ms）
    ui->spinBoxElapsedThreshold->setRange(0.1, 10000);
    ui->spinBoxElapsedThreshold->setValue(1000); // 默认阈值1000ms
    // 新增：连续异常次数输入框（范围1-20次）
    ui->spinBoxAbnormalCount->setRange(1, 20);
    ui->spinBoxAbnormalCount->setValue(3); // 默认连续3次

    connect(ui->startButton, &QPushButton::clicked, this, &ForwarderWindow::onStartClicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &ForwarderWindow::onStopClicked);

    connect(forwarder_, &Forwarder::statusUpdated, this, &ForwarderWindow::updateStatus);
    connect(forwarder_, &Forwarder::senderPacketCountChanged, this, &ForwarderWindow::updateSenderPacketCount);
    connect(forwarder_, &Forwarder::forwardPacketCountChanged, this, &ForwarderWindow::updateForwardPacketCount);
    connect(forwarder_, &Forwarder::receiverConnectionStatusChanged, this, &ForwarderWindow::updateReceiverConnectionStatus);
    // 新增：阈值修改信号（动态更新到forwarder_）
    connect(ui->spinBoxElapsedThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [=](double val) {
                forwarder_->setElapsedThreshold(val);
                updateStatus(QString("已更新中间模块时延阈值为：%1 ms").arg(val));
            });
    connect(ui->spinBoxAbnormalCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [=](int val) {
                forwarder_->setAbnormalCountThreshold(val);
                updateStatus(QString("已更新连续异常次数阈值为：%1 次").arg(val));
            });
    // 新增：连接告警信号
    connect(forwarder_, &Forwarder::elapsedAlarm, this, &ForwarderWindow::onElapsedAlarm);
    connect(forwarder_, &Forwarder::elapsedRecovered, this, &ForwarderWindow::onElapsedRecovered);
    // 初始化告警标签
    ui->alarmLabel->setText("告警信息：无");
    ui->alarmLabel->setStyleSheet("color: black;");


    ui->stopButton->setEnabled(false);
}

ForwarderWindow::~ForwarderWindow()
{
    // 先停止转发器，确保所有网络操作终止
    forwarder_->stop();
    // 断开与转发器的连接，避免析构时触发信号
    disconnect(forwarder_, nullptr, this, nullptr);
    delete forwarder_; // 手动释放转发器（如果不是用 parent 管理）
    delete ui;
}

void ForwarderWindow::onStartClicked()
{
    QString recvIp = ui->receiverIpEdit->text().trimmed();
    if (QHostAddress(recvIp).isNull()) {
        updateStatus("❌ 接收端IP格式错误");
        return;
    }

    int senderPort = ui->senderPortEdit->text().toInt();
    int receiverPort = ui->receiverPortEdit->text().toInt();

    forwarder_->start(senderPort, recvIp, receiverPort);

    ui->startButton->setEnabled(false);
    ui->stopButton->setEnabled(true);
}

void ForwarderWindow::onStopClicked()
{
    forwarder_->stop();

    ui->startButton->setEnabled(true);
    ui->stopButton->setEnabled(false);
}

void ForwarderWindow::updateStatus(const QString &msg)
{
    ui->logTextEdit->append(msg);
}

void ForwarderWindow::updateSenderPacketCount(int count)
{
    ui->senderPacketCountLabel->setText(QString::number(count));
}

void ForwarderWindow::updateForwardPacketCount(int count)
{
    ui->forwardPacketCountLabel->setText(QString::number(count));
}

void ForwarderWindow::updateReceiverConnectionStatus(bool connected)
{
    if (connected) {
        ui->receiverStatusLabel->setText("✅ 已连接");
    } else {
        ui->receiverStatusLabel->setText("❌ 未连接");
    }
}
// 新增：处理中间模块时延告警
void ForwarderWindow::onElapsedAlarm(const QString &alarmMsg)
{
    ui->alarmLabel->setText(alarmMsg);
    ui->alarmLabel->setStyleSheet("color: red; font-weight: bold;"); // 红色加粗显示告警
}

// 新增：处理中间模块时延恢复
void ForwarderWindow::onElapsedRecovered(const QString &recoverMsg)
{
    ui->alarmLabel->setText(recoverMsg);
    ui->alarmLabel->setStyleSheet("color: green;"); // 绿色显示恢复信息
}
