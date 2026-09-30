#include "receiver_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QDataStream>
#include <QLineEdit>
#include <QTimer>
#include <QComboBox>
#include <QNetworkInterface>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QQueue>
#include <QLabel>
#include <QtEndian>
#include <QPalette>
#include <QPainter>
#include <limits>

ReceiverWindow::ReceiverWindow(QWidget *parent)
    : QMainWindow(parent), m_receiverSocket(nullptr), m_tcpServer(nullptr),
    m_packetCounter(0), m_expectedSequence(0), m_firstSequence(0),
    m_isReceivingFile(false),
    m_latencyPointIndex(0),
    m_delayThreshold(1000),          // 默认处理时延阈值100ms
    m_abnormalCountThreshold(3),    // 默认连续3次超限告警
    m_abnormalDelayCount(0)
{
    // 初始化优先级队列和统计数据
    for (int i = 0; i < 4; ++i) {
        m_priorityQueues[i] = QQueue<PacketInfo>();
        m_queueLengths[i] = 0;
        m_totalQueueDelay[i] = 0.0;
        m_maxQueueDelay[i] = 0.0;
        m_packetCountPerQueue[i] = 0;
    }
    setupUI();
    setWindowTitle("组播/单播接收端");
    resize(800, 600);

    // 保存正常状态文本颜色
    m_normalStatusColor = m_statusLabel->palette().color(QPalette::WindowText);

    // 设置定时器以定期处理队列
    m_queueProcessTimer = new QTimer(this);
    m_queueProcessTimer->setTimerType(Qt::PreciseTimer);
    connect(m_queueProcessTimer, &QTimer::timeout, this, &ReceiverWindow::processPriorityQueues);
    m_queueProcessTimer->start(10); // 每10ms处理一次队列
}

void ReceiverWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // 时延阈值设置区域
    QHBoxLayout *thresholdLayout = new QHBoxLayout;
    thresholdLayout->addWidget(new QLabel("异常处理时延阈值:"));
    m_delayThresholdSpin = new QSpinBox(this);
    m_delayThresholdSpin->setRange(1, 10000);  // 1ms~10秒可调
    m_delayThresholdSpin->setValue(m_delayThreshold);  // 默认1000ms
    thresholdLayout->addWidget(m_delayThresholdSpin);

    thresholdLayout->addSpacing(20);
    thresholdLayout->addWidget(new QLabel("超过阈值次数:"));
    m_abnormalCountSpin = new QSpinBox(this);
    m_abnormalCountSpin->setRange(1, 10);  // 1~10次可调
    m_abnormalCountSpin->setValue(m_abnormalCountThreshold);  // 默认3次
    thresholdLayout->addWidget(m_abnormalCountSpin);
    mainLayout->addLayout(thresholdLayout);  // 添加到主布局

    // 配置组
    QGroupBox *configGroup = new QGroupBox("接收配置");
    QHBoxLayout *configLayout = new QHBoxLayout(configGroup);
    configLayout->addWidget(new QLabel("监听地址:"));
    m_listenAddressEdit = new QLineEdit("127.0.0.1", this);
    configLayout->addWidget(m_listenAddressEdit);
    configLayout->addWidget(new QLabel("端口:"));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(6000);
    configLayout->addWidget(m_portSpin);
    configLayout->addWidget(new QLabel("协议:"));
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItems({"UDP", "TCP"});
    configLayout->addWidget(m_protocolCombo);
    m_startButton = new QPushButton("开始接收", this);
    m_stopButton = new QPushButton("停止接收", this);
    m_stopButton->setEnabled(false);
    m_saveFileButton = new QPushButton("保存文件", this);
    m_saveFileButton->setEnabled(false);
    configLayout->addWidget(m_startButton);
    configLayout->addWidget(m_stopButton);
    configLayout->addWidget(m_saveFileButton);
    mainLayout->addWidget(configGroup);

    // 状态和告警信息
    QHBoxLayout *statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("状态: 未启动");
    m_alertStatusLabel = new QLabel("");  // 初始为空，不显示任何信息
    QPalette alertPalette = m_alertStatusLabel->palette();
    alertPalette.setColor(QPalette::WindowText, Qt::red);  // 告警文字为红色
    m_alertStatusLabel->setPalette(alertPalette);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addWidget(m_alertStatusLabel);
    mainLayout->addLayout(statusLayout);

    m_packetLossLabel = new QLabel("丢包率: 0.00%", this);
    mainLayout->addWidget(m_packetLossLabel);

    // 队列状态显示
    QGroupBox *queueStatsGroup = new QGroupBox("队列状态");
    QGridLayout *queueStatsLayout = new QGridLayout(queueStatsGroup);
    queueStatsLayout->addWidget(new QLabel("优先级"), 0, 0);
    queueStatsLayout->addWidget(new QLabel("队列长度"), 0, 1);
    queueStatsLayout->addWidget(new QLabel("平均排队时延 (ms)"), 0, 2);
    queueStatsLayout->addWidget(new QLabel("最大排队时延 (ms)"), 0, 3);
    for (int i = 0; i < 4; ++i) {
        m_queueLengthLabels[i] = new QLabel("0");
        m_avgQueueDelayLabels[i] = new QLabel("0.00");
        m_maxQueueDelayLabels[i] = new QLabel("0.00");
        queueStatsLayout->addWidget(new QLabel(QString("优先级 %1").arg(i)), i + 1, 0);
        queueStatsLayout->addWidget(m_queueLengthLabels[i], i + 1, 1);
        queueStatsLayout->addWidget(m_avgQueueDelayLabels[i], i + 1, 2);
        queueStatsLayout->addWidget(m_maxQueueDelayLabels[i], i + 1, 3);
    }
    mainLayout->addWidget(queueStatsGroup);

    // 数据包表格
    m_packetTable = new QTableWidget(this);
    m_packetTable->setColumnCount(7); // 添加优先级列
    m_packetTable->setHorizontalHeaderLabels({"序号", "源地址", "端口", "序列号", "时间戳", "传输时延", "优先级"});
    m_packetTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout->addWidget(m_packetTable);

    // 时延图表
    m_realtimeSeries = new QLineSeries();
    m_latencyChart = new QChart();
    m_latencyChart->addSeries(m_realtimeSeries);
    m_latencyChart->setTitle("实时时延曲线");
    m_axisX = new QValueAxis();
    m_axisX->setTitleText("包序号");
    m_axisX->setLabelFormat("%lld");
    m_axisX->setRange(0, 500);
    m_axisY = new QValueAxis();
    m_axisY->setTitleText("时延 (ms)");
    m_axisY->setRange(0, 20);
    m_latencyChart->addAxis(m_axisX, Qt::AlignBottom);
    m_latencyChart->addAxis(m_axisY, Qt::AlignLeft);
    m_realtimeSeries->attachAxis(m_axisX);
    m_realtimeSeries->attachAxis(m_axisY);
    m_chartView = new QChartView(m_latencyChart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(300);
    mainLayout->addWidget(m_chartView);

    // 开始：新增吞吐量图表
    m_throughputSeries = new QLineSeries();
    m_throughputSeries->setPointsVisible(true);
    m_throughputChart = new QChart();
    m_throughputChart->addSeries(m_throughputSeries);
    m_throughputChart->setTitle("实时吞吐量曲线 (Mbps)");
    m_throughputAxisX = new QValueAxis();
    m_throughputAxisX->setTitleText("组序号 (每100包一组)");
    m_throughputAxisX->setLabelFormat("%d");
    m_throughputAxisX->setRange(0, 10);
    m_throughputAxisY = new QValueAxis();
    m_throughputAxisY->setTitleText("吞吐量 (Mbps)");
    m_throughputAxisY->setRange(0, 100); // 根据预期吞吐量调整
    m_throughputChart->addAxis(m_throughputAxisX, Qt::AlignBottom);
    m_throughputChart->addAxis(m_throughputAxisY, Qt::AlignLeft);
    m_throughputSeries->attachAxis(m_throughputAxisX);
    m_throughputSeries->attachAxis(m_throughputAxisY);
    m_throughputChartView = new QChartView(m_throughputChart);
    m_throughputChartView->setRenderHint(QPainter::Antialiasing);
    m_throughputChartView->setMinimumHeight(300);
    mainLayout->addWidget(m_throughputChartView);

    // 信号连接
    connect(m_startButton, &QPushButton::clicked, this, &ReceiverWindow::onStartClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &ReceiverWindow::onStopClicked);
    connect(m_saveFileButton, &QPushButton::clicked, this, &ReceiverWindow::onSaveFileClicked);
    connect(m_protocolCombo, &QComboBox::currentTextChanged, this, &ReceiverWindow::onProtocolChanged);
    // 阈值设置信号连接
    connect(m_delayThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ReceiverWindow::onDelayThresholdChanged);
    connect(m_abnormalCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ReceiverWindow::onAbnormalCountChanged);
}

void ReceiverWindow::onProtocolChanged(const QString &protocol)
{
    if (protocol == "TCP") {
        m_listenAddressEdit->setPlaceholderText("请输入本地IP地址（例如 127.0.0.1 或 192.168.1.100）");
    } else {
        m_listenAddressEdit->setPlaceholderText("请输入监听地址（例如 239.255.0.1 或 127.0.0.1）");
    }
}

// 处理时延阈值变更
void ReceiverWindow::onDelayThresholdChanged(int threshold)
{
    m_delayThreshold = threshold;
    qDebug() << "处理时延阈值已更新为:" << m_delayThreshold << "ms";
}

// 连续超限次数阈值变更
void ReceiverWindow::onAbnormalCountChanged(int count)
{
    m_abnormalCountThreshold = count;
    qDebug() << "连续超限次数阈值已更新为:" << m_abnormalCountThreshold << "次";
}

bool ReceiverWindow::isValidLocalAddress(const QHostAddress &addr)
{
    if (addr == QHostAddress::AnyIPv4 || addr == QHostAddress::LocalHost) {
        return true;
    }
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip() == addr) {
                return true;
            }
        }
    }
    return false;
}

bool ReceiverWindow::configureSocket(const QHostAddress &listenAddr, quint16 port, const QString &protocol)
{
    if (m_receiverSocket || m_tcpServer) {
        QMessageBox::critical(this, "错误", "已有监听的 socket，请先停止接收");
        return false;
    }
    if (protocol == "UDP") {
        m_receiverSocket = new QUdpSocket(this);
        m_receiverSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 50*1024*1024);
        if (!m_receiverSocket->bind(listenAddr, port, QUdpSocket::ShareAddress)) {
            QMessageBox::critical(this, "错误", "无法绑定 " + listenAddr.toString() + ":" + QString::number(port));
            delete m_receiverSocket;
            m_receiverSocket = nullptr;
            return false;
        }
        if (listenAddr.isMulticast()) {
            if (!m_receiverSocket->joinMulticastGroup(listenAddr)) {
                QMessageBox::critical(this, "错误", "无法加入组播组 " + listenAddr.toString());
                delete m_receiverSocket;
                m_receiverSocket = nullptr;
                return false;
            }
        }
        connect(m_receiverSocket, &QUdpSocket::readyRead, this, &ReceiverWindow::processPendingDatagrams);
    } else if (protocol == "TCP") {
        if (!isValidLocalAddress(listenAddr)) {
            QMessageBox::critical(this, "错误", "无效的本地地址: " + listenAddr.toString());
            return false;
        }
        m_tcpServer = new QTcpServer(this);
        if (!m_tcpServer->listen(listenAddr, port)) {
            QMessageBox::critical(this, "错误", "无法监听 " + listenAddr.toString() + ":" + QString::number(port));
            delete m_tcpServer;
            m_tcpServer = nullptr;
            return false;
        }
        connect(m_tcpServer, &QTcpServer::newConnection, this, &ReceiverWindow::onNewConnection);
    }
    return true;
}

void ReceiverWindow::onStartClicked()
{
    QHostAddress listenAddr(m_listenAddressEdit->text().trimmed());
    if (listenAddr.isNull()) {
        QMessageBox::critical(this, "错误", "请输入有效的监听地址");
        return;
    }
    quint16 port = static_cast<quint16>(m_portSpin->value());
    QString protocol = m_protocolCombo->currentText();
    if (!configureSocket(listenAddr, port, protocol)) {
        return;
    }
    m_statusLabel->setText("状态: 正在接收 " + listenAddr.toString() + ":" + QString::number(port) + " (" + protocol + ")");
    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_packetCounter = 0;
    m_expectedSequence = 0;
    m_firstSequence = 0;
    m_receivedSequences.clear();
    m_batchMap.clear();
    m_clientBuffers.clear();
    m_packetTable->setRowCount(0);
    m_packetLossLabel->setText("丢包率: 0.00%");
    resetFileReceiving();
    resetAlertStatus();  // 重置告警状态

    // 重置队列和统计数据
    for (int i = 0; i < 4; ++i) {
        m_priorityQueues[i].clear();
        m_queueLengths[i] = 0;
        m_totalQueueDelay[i] = 0.0;
        m_maxQueueDelay[i] = 0.0;
        m_packetCountPerQueue[i] = 0;
        m_queueLengthLabels[i]->setText("0");
        m_avgQueueDelayLabels[i]->setText("0.00");
        m_maxQueueDelayLabels[i]->setText("0.00");
    }
    m_queueProcessTimer->start(10);
    //开始： 重置吞吐量相关变量和图表
    m_throughputSeries->clear();
    m_throughputPointIndex = 1;     // 从1开始作为第1组
    m_currentGroupPacketCount = 0;
    m_currentGroupTotalBytes = 0;
    m_currentGroupStartTime = 0;
    // X 轴初始范围设为 1..1（或 1..10 也可以），并设置刻度数为1，避免第一组时出现 1..2 的小区间问题
    m_throughputAxisX->setRange(1, 1);
    m_throughputAxisX->setTickCount(1);
    m_throughputAxisY->setRange(0, 1);
    m_throughputChart->update();
    //结束。
}

void ReceiverWindow::onStopClicked()
{
    if (m_receiverSocket) {
        QHostAddress listenAddr(m_listenAddressEdit->text());
        if (listenAddr.isMulticast()) {
            m_receiverSocket->leaveMulticastGroup(listenAddr);
        }
        m_receiverSocket->close();
        m_receiverSocket->deleteLater();
        m_receiverSocket = nullptr;
    }
    if (m_tcpServer) {
        for (QTcpSocket *client : qAsConst(m_tcpClients)) {
            if (client) {
                client->disconnect();
                client->close();
                client->deleteLater();
            }
        }
        m_tcpClients.clear();
        m_clientBuffers.clear();
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
    m_statusLabel->setText("状态: 已停止");
    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_packetCounter = 0;
    m_expectedSequence = 0;
    m_firstSequence = 0;
    m_receivedSequences.clear();
    m_batchMap.clear();
    m_packetLossLabel->setText("丢包率: 0.00%");
    resetFileReceiving();
    resetAlertStatus();  // 重置告警状态

    // 停止队列处理并清空
    m_queueProcessTimer->stop();
    for (int i = 0; i < 4; ++i) {
        m_priorityQueues[i].clear();
        m_queueLengths[i] = 0;
        m_totalQueueDelay[i] = 0.0;
        m_maxQueueDelay[i] = 0.0;
        m_packetCountPerQueue[i] = 0;
        m_queueLengthLabels[i]->setText("0");
        m_avgQueueDelayLabels[i]->setText("0.00");
        m_maxQueueDelayLabels[i]->setText("0.00");
    }
    if (m_realtimeSeries) {
        m_realtimeSeries->clear();
        m_latencyPointIndex = 0;
        m_axisX->setRange(0, 1000);
        m_axisY->setRange(0, 10);
        m_latencyChart->update();
        //开始：
        if (m_throughputSeries) {
            m_throughputSeries->clear();
            m_throughputPointIndex = 0;
            m_throughputAxisX->setRange(0, 10);
            m_throughputAxisY->setRange(0, 1);
            m_throughputChart->update();
        }
        m_currentGroupPacketCount = 0;
        m_currentGroupTotalBytes = 0;
        m_currentGroupStartTime = 0;
        //结束。
    }
}

// 重置告警状态
void ReceiverWindow::resetAlertStatus()
{
    m_abnormalDelayCount = 0;
    m_alertStatusLabel->setText("");  // 清空告警信息
    QPalette palette = m_statusLabel->palette();
    palette.setColor(QPalette::WindowText, m_normalStatusColor);
    m_statusLabel->setPalette(palette);
}

// 判断处理时延是否超限并更新UI
void ReceiverWindow::checkDurationAbnormal(double durationMs, quint32 batchNumber)
{
    QPalette palette = m_statusLabel->palette();

    if (durationMs > m_delayThreshold) {
        // 超限：增加连续计数
        m_abnormalDelayCount++;
        palette.setColor(QPalette::WindowText, Qt::red);  // 红色表示异常
        m_statusLabel->setPalette(palette);

        // 仅当连续超限次数达到阈值时，才在UI显示超限信息
        if (m_abnormalDelayCount >= m_abnormalCountThreshold) {
            m_alertStatusLabel->setText(
                QString("告警: 批次%1处理时延超限（%2ms），连续超限%3次")
                    .arg(batchNumber)
                    .arg(durationMs, 0, 'f', 2)
                    .arg(m_abnormalDelayCount)
                );
        } else {
            // 未达阈值，不显示告警信息
            m_alertStatusLabel->setText("");
        }
    } else {
        // 未超限：重置连续计数，恢复正常颜色
        m_abnormalDelayCount = 0;
        palette.setColor(QPalette::WindowText, m_normalStatusColor);
        m_statusLabel->setPalette(palette);
        m_alertStatusLabel->setText("");  // 清空告警信息
    }
}

void ReceiverWindow::resetFileReceiving()
{
    m_fileTransfers.clear();
    m_completedRanges.clear();
    m_isReceivingFile = false;
}

void ReceiverWindow::failFileTransfer(quint32 batchNumber, const QString &reason)
{
    qWarning() << "文件批次" << batchNumber << reason;
    m_fileTransfers.remove(batchNumber);
    m_isReceivingFile = !m_fileTransfers.isEmpty();
    m_statusLabel->setText(QString("状态: 文件批次 %1 失败：%2").arg(batchNumber).arg(reason));
}

void ReceiverWindow::handleFilePacket(const PacketInfo &info)
{
    constexpr quint64 maxBufferedBytes = 64ULL * 1024 * 1024;
    if (info.totalPacketCount == 0) {
        failFileTransfer(info.batchNumber, "包数无效");
        return;
    }

    auto finished = m_completedRanges.constFind(info.batchNumber);
    if (finished != m_completedRanges.cend()) {
        if (info.isFileHeader) {
            // 发送进程重启后可能重新使用批次号与序列号；新文件头开始新传输。
            m_completedRanges.remove(info.batchNumber);
        } else if (info.sequence >= finished.value().first && info.sequence <= finished.value().second) {
            return; // UDP重发的旧分片
        }
    }

    FileTransfer &transfer = m_fileTransfers[info.batchNumber];
    m_isReceivingFile = true;
    if (transfer.totalPackets == 0) transfer.totalPackets = info.totalPacketCount;
    if (transfer.totalPackets != info.totalPacketCount) {
        failFileTransfer(info.batchNumber, "分片总数不一致");
        return;
    }

    if (info.isFileHeader) {
        if (transfer.headerReceived) return; // 重复的文件头
        if (info.fileData.size() > 4096 ||
            info.sequence > std::numeric_limits<quint64>::max() - (transfer.totalPackets - 1)) {
            failFileTransfer(info.batchNumber, "文件头无效");
            return;
        }
        QDataStream metadata(info.fileData);
        metadata.setByteOrder(QDataStream::LittleEndian);
        quint64 fileSize = 0;
        QString fileName;
        metadata >> fileSize >> fileName;
        fileName.replace('\\', '/');
        fileName = QFileInfo(fileName).fileName();
        if (metadata.status() != QDataStream::Ok || fileName.isEmpty() || metadata.device()->bytesAvailable() != 0) {
            failFileTransfer(info.batchNumber, "无法解析文件名或大小");
            return;
        }
        QSharedPointer<QTemporaryFile> tempFile(
            new QTemporaryFile(QDir::tempPath() + "/qt_receiver_XXXXXX.part"));
        if (!tempFile->open()) {
            failFileTransfer(info.batchNumber, "无法创建临时文件");
            return;
        }
        transfer.headerReceived = true;
        transfer.headerSequence = info.sequence;
        transfer.fileSize = fileSize;
        transfer.fileName = fileName;
        transfer.tempFile = tempFile;
        const quint64 lastSequence = info.sequence + transfer.totalPackets - 1;
        for (auto it = transfer.pendingChunks.begin(); it != transfer.pendingChunks.end();) {
            if (it.key() <= info.sequence || it.key() > lastSequence) {
                transfer.bufferedBytes -= static_cast<quint64>(it.value().size());
                it = transfer.pendingChunks.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        if (transfer.headerReceived) {
            const quint64 firstData = transfer.headerSequence + 1;
            const quint64 lastData = transfer.headerSequence + transfer.totalPackets - 1;
            if (info.sequence < firstData || info.sequence > lastData ||
                info.sequence < firstData + transfer.writtenPackets) return;
        }
        if (transfer.pendingChunks.contains(info.sequence)) return;
        if (transfer.bufferedBytes + static_cast<quint64>(info.fileData.size()) > maxBufferedBytes) {
            failFileTransfer(info.batchNumber, "等待缺失分片时缓存超过 64 MiB");
            return;
        }
        transfer.pendingChunks.insert(info.sequence, info.fileData);
        transfer.bufferedBytes += static_cast<quint64>(info.fileData.size());
    }

    if (!transfer.headerReceived) return;
    while (transfer.writtenPackets < transfer.totalPackets - 1) {
        const quint64 nextSequence = transfer.headerSequence + 1 + transfer.writtenPackets;
        auto it = transfer.pendingChunks.find(nextSequence);
        if (it == transfer.pendingChunks.end()) break;
        const QByteArray chunk = it.value();
        if (static_cast<quint64>(chunk.size()) > transfer.fileSize - transfer.writtenBytes ||
            transfer.tempFile->write(chunk) != chunk.size()) {
            failFileTransfer(info.batchNumber, "文件长度超限或临时文件写入失败");
            return;
        }
        transfer.writtenBytes += static_cast<quint64>(chunk.size());
        transfer.writtenPackets++;
        transfer.bufferedBytes -= static_cast<quint64>(chunk.size());
        transfer.pendingChunks.erase(it);
    }

    if (transfer.writtenPackets == transfer.totalPackets - 1) {
        if (transfer.writtenBytes != transfer.fileSize || !transfer.tempFile->flush()) {
            failFileTransfer(info.batchNumber, "接收字节数与文件大小不符");
            return;
        }
        // 先物化文件名；Linux 上临时文件可能最初没有目录项。
        transfer.tempFile->fileName();
        transfer.tempFile->close();
        const QString completedName = transfer.fileName;
        m_completedFiles.enqueue({completedName, transfer.tempFile});
        m_completedRanges.insert(info.batchNumber,
                                 qMakePair(transfer.headerSequence,
                                           transfer.headerSequence + transfer.totalPackets - 1));
        m_fileTransfers.remove(info.batchNumber);
        m_isReceivingFile = !m_fileTransfers.isEmpty();
        m_saveFileButton->setEnabled(true);
        m_statusLabel->setText(QString("状态: 文件 %1 接收完成，可点击保存文件（待保存 %2 个）")
                                   .arg(completedName).arg(m_completedFiles.size()));
    }
}

void ReceiverWindow::onNewConnection()
{
    if (!m_tcpServer) {
        qDebug() << "TCP服务器未初始化，忽略新连接";
        return;
    }
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket *client = m_tcpServer->nextPendingConnection();
        if (!client) {
            qDebug() << "无效的客户端连接";
            continue;
        }
        m_tcpClients.append(client);
        client->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024);
        connect(client, &QTcpSocket::readyRead, this, &ReceiverWindow::processTcpData);
        connect(client, &QTcpSocket::disconnected, this, [this, client]() {
            m_tcpClients.removeOne(client);
            m_clientBuffers.remove(client);
            client->deleteLater();
            qDebug() << "客户端断开连接:" << client->peerAddress().toString();
            if (m_isReceivingFile && m_tcpClients.isEmpty()) {
                resetFileReceiving();
                m_statusLabel->setText("状态: 文件传输中断");
            }
        });
        connect(client, &QTcpSocket::errorOccurred, this, [this, client](QAbstractSocket::SocketError) {
            qDebug() << "客户端错误:" << client->errorString();
            m_tcpClients.removeOne(client);
            m_clientBuffers.remove(client);
            client->deleteLater();
            if (m_isReceivingFile && m_tcpClients.isEmpty()) {
                resetFileReceiving();
                m_statusLabel->setText("状态: 文件传输中断");
            }
        });
        qDebug() << "新客户端连接:" << client->peerAddress().toString() << ":" << client->peerPort();
    }
}

void ReceiverWindow::onSaveFileClicked()
{
    if (m_completedFiles.isEmpty()) return;
    const CompletedFile &completed = m_completedFiles.head();

    const QString savePath = QFileDialog::getSaveFileName(
        this, tr("保存文件"), completed.fileName, tr("所有文件 (*.*)"));
    if (savePath.isEmpty()) return;

    QFile source(completed.tempFile->fileName());
    QSaveFile destination(savePath);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        m_statusLabel->setText("状态: 无法打开临时文件或保存位置");
        return;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() || destination.write(block) != block.size()) {
            destination.cancelWriting();
            m_statusLabel->setText("状态: 保存文件失败");
            return;
        }
    }
    if (!destination.commit()) {
        m_statusLabel->setText("状态: 保存文件失败");
        return;
    }
    m_completedFiles.dequeue();
    m_saveFileButton->setEnabled(!m_completedFiles.isEmpty());
    m_statusLabel->setText(QString("状态: 文件 %1 保存成功").arg(savePath));
}

void ReceiverWindow::updatePacketLossRate()
{
    if (m_packetCounter == 0 || m_expectedSequence < m_firstSequence) {
        m_packetLossLabel->setText("丢包率: 0.00%");
        return;
    }

    quint64 expectedPackets = m_expectedSequence - m_firstSequence+1; // 包含首个包
    quint64 lostPackets = (expectedPackets > m_packetCounter) ? (expectedPackets - m_packetCounter) : 0;
    double lossRate = (static_cast<double>(lostPackets) / expectedPackets) * 100.0;

    m_packetLossLabel->setText(QString("丢包率: %1%").arg(lossRate, 0, 'f', 2));
    qDebug() << "丢包率更新: 接收数据包数=" << m_packetCounter
             << ", 期望数据包数=" << expectedPackets
             << ", 丢失数据包数=" << lostPackets
             << ", 丢包率=" << lossRate << "%";
}

void ReceiverWindow::processPendingDatagrams()
{
    if (!m_receiverSocket) return;

    while (m_receiverSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_receiverSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;
        m_receiverSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        constexpr int headerSize = 32;
        constexpr int crcSize = 2;
        if (datagram.size() < headerSize + crcSize) {
            qWarning() << "UDP数据包过短";
            continue;
        }

        QDataStream stream(datagram);
        quint16 header;
        quint64 sequence;
        qint64 timestamp;
        quint32 payloadSize;
        quint8 packetType;
        quint8 priority;
        quint32 totalPacketCount;
        quint32 batchNumber;
        quint16 crc;
        // 开始：新增：记录整个数据包大小用于吞吐量计算
        quint32 packetSize;
        // 结束

        stream >> header >> sequence >> timestamp >> payloadSize >> packetType >> priority >> totalPacketCount >> batchNumber;
        qDebug() << "收到序号:" << sequence << ", 当前期望序号:" << m_expectedSequence;

        if (header != 0xAA55) {
            qDebug() << "无效数据包头，跳过";
            continue;
        }

        if (payloadSize != static_cast<quint32>(datagram.size() - headerSize - crcSize)) {
            qWarning() << "UDP负载长度不匹配";
            continue;
        }

        QByteArray payload;
        payload.resize(payloadSize);
        if (stream.readRawData(payload.data(), payloadSize) != static_cast<int>(payloadSize)) {
            qWarning() << "UDP负载不完整";
            continue;
        }
        stream >> crc;

        quint16 calculatedCrc = qChecksum(datagram.constData(), datagram.size() - sizeof(quint16));
        if (crc != calculatedCrc) {
            qDebug() << "CRC校验失败，跳过";
            continue;
        }

        if (priority > 3) {
            qWarning() << "无效优先级，设置为0";
            priority = 0;
        }

        bool isNewPacket = false;
        if (!m_receivedSequences.contains(sequence)) {
            m_receivedSequences.insert(sequence);
            isNewPacket = true;
            if (m_receivedSequences.size() == 1) {
                m_firstSequence = sequence;
                m_expectedSequence = sequence;
                qDebug() << "seq:" << sequence << ", old expected:" << m_expectedSequence;
            }
            else if (sequence > m_expectedSequence) {
                m_expectedSequence = sequence;
                qDebug() << "updated expected sequence to:" << m_expectedSequence;
            }

        } else {
            qDebug() << "重复包序号:" << sequence << "来自" << sender.toString();
        }

        qint64 now = m_clock.currentTime();

        // 更新批次信息
        BatchInfo &batch = m_batchMap[batchNumber];
        batch.batchNumber = batchNumber;
        if (batch.startTime == 0) {
            batch.startTime = now;
            batch.totalPacketCount = totalPacketCount;
        }
        batch.receivedPacketCount++;

        double latencyMs = (now - timestamp) / 1000000.0;
        qDebug() << now;
        bool isFilePacket = (packetType == 1 || packetType == 2);

        PacketInfo info;
        info.counter = (isNewPacket) ? ++m_packetCounter : m_packetCounter;
        info.sender = sender;
        info.senderPort = senderPort;
        info.sequence = sequence;
        info.timestamp = timestamp;
        info.latencyMs = latencyMs;
        info.isFilePacket = isFilePacket;
        info.isFileHeader = (packetType == 2);
        info.fileData = payload;
        info.priority = priority;
        info.enqueueTime = now;
        info.batchNumber = batchNumber;
        info.totalPacketCount = totalPacketCount;
        info.clientSocket = nullptr;
        info.senderAddress = sender;
        // 开始：新增，记录数据包大小（datagram包含整个数据包的字节数）
        info.packetSize = static_cast<quint32>(datagram.size());
        // 结束

        m_priorityQueues[priority].enqueue(info);
        m_queueLengths[priority]++;
        if (m_queueLengthLabels[priority])
            m_queueLengthLabels[priority]->setText(QString::number(m_queueLengths[priority]));

        if (isNewPacket) {
            appendLatencyPoint(latencyMs);
            QMetaObject::invokeMethod(this, "updateLatencyChart", Qt::QueuedConnection);
        }
    }

    updatePacketLossRate();
}


void ReceiverWindow::processTcpData()
{
    if (m_tcpClients.isEmpty()) return;

    QList<QTcpSocket*> clientsToRemove;

    for (QTcpSocket* client : m_tcpClients) {
        if (!client || client->state() != QAbstractSocket::ConnectedState) {
            if (client) clientsToRemove.append(client);
            continue;
        }

        QByteArray &buffer = m_clientBuffers[client];
        buffer.append(client->readAll());

        while (true) {
            if (buffer.size() < static_cast<int>(sizeof(quint32)))
                break;

            QDataStream sizeStream(buffer);
            sizeStream.setByteOrder(QDataStream::BigEndian);
            quint32 packetSize = 0;
            sizeStream >> packetSize;

            if (packetSize == 0 || packetSize > 65535) {
                qDebug() << "无效TCP数据包大小:" << packetSize;
                client->close();
                clientsToRemove.append(client);
                break;
            }

            if (buffer.size() < static_cast<int>(sizeof(quint32) + packetSize))
                break;

            QByteArray datagram = buffer.mid(sizeof(quint32), packetSize);
            buffer.remove(0, sizeof(quint32) + packetSize);

            QDataStream packetStream(datagram);
            packetStream.setByteOrder(QDataStream::BigEndian);

            quint16 header;
            quint64 sequence;
            qint64 timestamp;
            quint32 payloadSize;
            quint8 packetType;
            quint8 priority;
            quint32 totalPacketCount;
            quint32 batchNumber;
            quint16 crc;



            packetStream >> header >> sequence >> timestamp >> payloadSize >> packetType >> priority >> totalPacketCount >> batchNumber;
            qDebug() << "收到序号:" << sequence << ", 当前期望序号:" << m_expectedSequence;

            if (header != 0xAA55) {
                qDebug() << "无效TCP数据包头，跳过";
                continue;
            }

            qint64 expectedSize = sizeof(quint16) + sizeof(quint64) + sizeof(qint64) + sizeof(quint32) + sizeof(quint8) + sizeof(quint8) + payloadSize + sizeof(quint16) + sizeof(quint32) + sizeof(quint32);
            if (datagram.size() != expectedSize) {
                qDebug() << "TCP数据包大小不匹配";
                continue;
            }

            QByteArray payload;
            payload.resize(payloadSize);
            qint64 readLen = packetStream.readRawData(payload.data(), payloadSize);
            if (readLen != payloadSize) {
                qDebug() << "读取负载失败";
                continue;
            }

            packetStream >> crc;

            quint16 calculatedCrc = qChecksum(datagram.constData(), datagram.size() - sizeof(quint16));
            if (crc != calculatedCrc) {
                qDebug() << "CRC校验失败，跳过";
                continue;
            }

            if (priority > 3) {
                qWarning() << "无效优先级，设置为0";
                priority = 0;
            }

            qint64 now = m_clock.currentTime();

            // 更新批次信息
            BatchInfo &batch = m_batchMap[batchNumber];
            batch.batchNumber = batchNumber;
            if (batch.startTime == 0) {
                batch.startTime = now;
                batch.totalPacketCount = totalPacketCount;
            }
            batch.receivedPacketCount++;

            double latencyMs = (now - timestamp) / 1'000'000.0;
            bool isFilePacket = (packetType == 1 || packetType == 2);

            PacketInfo info = {
                ++m_packetCounter,
                client->peerAddress(),
                client->peerPort(),
                sequence,
                timestamp,
                latencyMs,
                isFilePacket,
                packetType == 2,
                payload,
                priority,
                now,
                batchNumber,
                totalPacketCount,
                client,
                QHostAddress(),
                static_cast<quint32>(packetSize + sizeof(quint32)) // 新增：TCP 额外的前导大小字段
            };

            m_priorityQueues[priority].enqueue(info);
            m_queueLengths[priority]++;
            m_queueLengthLabels[priority]->setText(QString::number(m_queueLengths[priority]));

            appendLatencyPoint(latencyMs);
        }
    }

    for (QTcpSocket *client : qAsConst(clientsToRemove)) {
        m_tcpClients.removeOne(client);
        m_clientBuffers.remove(client);
        client->deleteLater();
    }

    updatePacketLossRate();
}

void ReceiverWindow::processPriorityQueues()
{
    const qint64 sliceStartTime = m_clock.currentTime();
    qint64 currentTime = sliceStartTime;

    for (int priority = 0; priority < 4; ++priority) {
        while (!m_priorityQueues[priority].isEmpty()) {
            currentTime = m_clock.currentTime();
            const qint64 packetWorkStartNs = currentTime;
            PacketInfo info = m_priorityQueues[priority].dequeue();
            m_queueLengths[priority]--;
            m_queueLengthLabels[priority]->setText(QString::number(m_queueLengths[priority]));

            double queueDelayMs = (currentTime - info.enqueueTime) / 1'000'000.0;
            m_packetCountPerQueue[priority]++;
            m_totalQueueDelay[priority] += queueDelayMs;
            m_maxQueueDelay[priority] = qMax(m_maxQueueDelay[priority], queueDelayMs);
            m_avgQueueDelayLabels[priority]->setText(QString::number(m_totalQueueDelay[priority] / m_packetCountPerQueue[priority], 'f', 2));
            m_maxQueueDelayLabels[priority]->setText(QString::number(m_maxQueueDelay[priority], 'f', 2));

            addPacketToTable(info.counter, info.sender, info.senderPort, info.sequence,
                             info.timestamp, info.latencyMs, info.isFilePacket, info.priority);
            if (info.isFilePacket) handleFilePacket(info);

            // 批次处理计数及耗时统计
            if (m_batchMap.contains(info.batchNumber)) {
                BatchInfo &batch = m_batchMap[info.batchNumber];
                batch.processedPacketCount++;
                batch.endTime = currentTime;
                batch.processingNs += qMax<qint64>(0, m_clock.currentTime() - packetWorkStartNs);

                if (batch.processedPacketCount == batch.totalPacketCount) {
                    // 按逐包本地处理时间求和，不包含发送端限速导致的包间等待。
                    double durationMs = batch.processingNs / 1'000'000.0;

                    qDebug() << "批次" << info.batchNumber
                             << "所有包处理完成，处理阶段耗时：" << durationMs << " ms";

                    // 检查是否超限并更新UI
                    checkDurationAbnormal(durationMs, info.batchNumber);

                    // 发送反馈
                    if (info.clientSocket) {
                        sendFeedbackToSender(QHostAddress(), 0, info.clientSocket, batch.batchNumber, durationMs);
                    } else {
                        sendFeedbackToSender(info.senderAddress, info.senderPort, nullptr, batch.batchNumber, durationMs);
                    }

                    m_statusLabel->setText(QString("批次 %1 处理完成（处理阶段耗时: %2 ms）")
                                               .arg(info.batchNumber).arg(durationMs, 0, 'f', 2));

                    m_batchMap.remove(info.batchNumber);
                }
            }

            //开始：新增： 吞吐量计算（对所有数据包）
            if (m_currentGroupPacketCount == 0 || m_currentGroupStartTime < 0) {
                // 第一个包时初始化起始时间
                m_currentGroupStartTime = info.enqueueTime;
                m_currentGroupTotalBytes = 0;
                qDebug() << "开始新组，起始时间:" << m_currentGroupStartTime;
            }
            m_currentGroupTotalBytes += info.packetSize;
            m_currentGroupPacketCount++;
            qDebug() << "当前组: 包数=" << m_currentGroupPacketCount << ", 总字节=" << m_currentGroupTotalBytes << ", 数据包大小=" << info.packetSize;

            if (m_currentGroupPacketCount >= 100) {
                qint64 groupEndTime = info.enqueueTime;
                double timeDiffSeconds = (groupEndTime - m_currentGroupStartTime) / 1'000'000'000.0;
                qDebug() << "时间差:" << timeDiffSeconds << "秒, 组结束时间:" << groupEndTime << ", 起始时间:" << m_currentGroupStartTime;
                if (timeDiffSeconds > 0) {
                    double throughputMbps = (m_currentGroupTotalBytes * 8.0) / timeDiffSeconds / 1'000'000.0;
                    if (throughputMbps > 0.0) {   // 只有 >0 时才追加点
                        appendThroughputPoint(throughputMbps);
                        qDebug() << "组吞吐量:" << throughputMbps << "Mbps (组序号:" << m_throughputPointIndex - 1 << ")";
                    } else {
                        qDebug() << "吞吐量计算结果为0，跳过";
                    }
                } else {
                    qDebug() << "时间差为零或负数，跳过吞吐量计算";
                }
                m_currentGroupPacketCount = 0;
                m_currentGroupTotalBytes = 0;
                m_currentGroupStartTime = 0;
            }
            //结束

            updatePacketLossRate();
            QMetaObject::invokeMethod(this, "updateLatencyChart", Qt::QueuedConnection);
            // 控制处理速度，避免阻塞UI
            if (m_clock.currentTime() - sliceStartTime > 5'000'000) break; // 5ms
            QMetaObject::invokeMethod(this, "updateThroughputChart", Qt::QueuedConnection); //开始： 新增
        }
    }
}



void ReceiverWindow::updateLatencyChart()
{
    if (!m_latencyChart || !m_chartView) return;
    m_chartView->update();
}

void ReceiverWindow::addPacketToTable(quint64 counter, const QHostAddress &sender,
                                      quint16 port, quint64 sequence,
                                      qint64 timestamp, double latencyMs, bool isFilePacket, quint8 priority)
{
    int row = m_packetTable->rowCount();
    m_packetTable->insertRow(row);
    m_packetTable->setItem(row, 0, new QTableWidgetItem(QString::number(counter)));
    m_packetTable->setItem(row, 1, new QTableWidgetItem(sender.toString()));
    m_packetTable->setItem(row, 2, new QTableWidgetItem(QString::number(port)));
    m_packetTable->setItem(row, 3, new QTableWidgetItem(QString::number(sequence)));
    m_packetTable->setItem(row, 4, new QTableWidgetItem(QString::number(timestamp)));
    m_packetTable->setItem(row, 5, new QTableWidgetItem(QString::number(latencyMs, 'f', 2) + " ms" + (isFilePacket ? " (文件)" : "")));
    m_packetTable->setItem(row, 6, new QTableWidgetItem(QString::number(priority)));
    m_packetTable->scrollToBottom();
}

int ReceiverWindow::currentTestMbps() const {
    return m_bandwidthSelector ? m_bandwidthSelector->currentText().toInt() : 0;
}

void ReceiverWindow::appendLatencyPoint(double latencyMs) {
    if (!m_realtimeSeries) return;
    m_realtimeSeries->append(m_latencyPointIndex++, latencyMs);
    if (m_realtimeSeries->count() > 1000) {
        m_realtimeSeries->removePoints(0, m_realtimeSeries->count() - 1000);
    }
    m_axisX->setRange(qMax(0ull, m_latencyPointIndex - 1000), m_latencyPointIndex);
    m_axisY->setRange(0, qMax(10.0, latencyMs * 1.5));
}

QVector<int> ReceiverWindow::getQueueLengths() const
{
    QVector<int> lengths(4);
    for (int i = 0; i < 4; ++i) {
        lengths[i] = m_queueLengths[i];
    }
    return lengths;
}

QVector<qint64> ReceiverWindow::getAverageQueueDelays() const
{
    QVector<qint64> delays(4);
    for (int i = 0; i < 4; ++i) {
        delays[i] = m_packetCountPerQueue[i] > 0 ? static_cast<qint64>(m_totalQueueDelay[i] * 1000 / m_packetCountPerQueue[i]) : 0;
    }
    return delays;
}

QVector<qint64> ReceiverWindow::getMaxQueueDelays() const
{
    QVector<qint64> delays(4);
    for (int i = 0; i < 4; ++i) {
        delays[i] = static_cast<qint64>(m_maxQueueDelay[i] * 1000);
    }
    return delays;
}

void ReceiverWindow::sendFeedbackToSender(const QHostAddress &addr, quint16 port, QTcpSocket *tcpSocket, quint32 batchNumber, double durationMs)
{
    qDebug() << "sendFeedbackToSender called, batch:" << batchNumber << "durationMs:" << durationMs;

    QByteArray feedback;
    QDataStream out(&feedback, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);

    out << quint32(0xFEEDC0DE);
    out << batchNumber;

    // 转换float为大端字节序整数再写入
    float durationFloat = static_cast<float>(durationMs);
    // 手动转换字节序
    quint32 durationInt;
    memcpy(&durationInt, &durationFloat, sizeof(float));
    durationInt = qToBigEndian(durationInt);
    out << durationInt;

    qDebug() << "反馈包大小:" << feedback.size();  // 应输出12
    qDebug() << "反馈包内容:" << feedback.toHex(' ').toUpper();

    if (tcpSocket) {
        qDebug() << "tcpSocket state:" << tcpSocket->state();
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            qint64 bytesWritten = tcpSocket->write(feedback);
            qDebug() << "TCP write bytes:" << bytesWritten << "/" << feedback.size();
            tcpSocket->flush();
        } else {
            qDebug() << "tcpSocket not connected, cannot send feedback";
        }
    } else if (m_receiverSocket) {
        qint64 bytesWritten = m_receiverSocket->writeDatagram(feedback, addr, port);
        m_receiverSocket->flush();
        qDebug() << "UDP writeDatagram bytes:" << bytesWritten;
    }
}

//开始：新增
void ReceiverWindow::appendThroughputPoint(double throughputMbps) {
    if (!m_throughputSeries) return;

    m_throughputSeries->append(m_throughputPointIndex, throughputMbps);
    m_throughputPointIndex++;

    const int window = 10;
    if (m_throughputSeries->count() > window) {
        m_throughputSeries->removePoints(0, m_throughputSeries->count() - window);
    }

    int visibleMax = m_throughputPointIndex - 1;
    int visibleMin = qMax(1, visibleMax - (window - 1));

    double maxThroughput = 0.0;
    for (const QPointF &point : m_throughputSeries->points()) {
        maxThroughput = qMax(maxThroughput, point.y());
    }
    double upper = qMax(10.0, maxThroughput * 1.2);
    m_throughputAxisY->setRange(0, upper);

    if (visibleMin == visibleMax) {
        // 针对单点情况，以数据点为中心设置范围
        m_throughputAxisX->setRange(visibleMax, visibleMax + 1);
        m_throughputAxisX->setTickCount(2);
    } else {
        m_throughputAxisX->setRange(visibleMin, visibleMax);
        m_throughputAxisX->setTickCount(visibleMax - visibleMin + 1);
    }

    m_throughputChartView->update();
}
//开始：新增
void ReceiverWindow::updateThroughputChart()
{
    if (!m_throughputChart || !m_throughputChartView) return;
    m_throughputChartView->update();
}
//结束
