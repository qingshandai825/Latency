#include <mainwindow.h>
#include <ui_mainwindow.h>
#include <PacketReceiver.h>
#include <QTimer>
#include <QBuffer>
#include <QtEndian>
#include <QVBoxLayout>
#include <QDebug>

// 基类从QMainWindow改为QWidget
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::MainWindow),  // 基类初始化修改
    clientSocket(new QTcpSocket(this)), server(new QTcpServer(this)), serverClientSocket(nullptr)
{
    ui->setupUi(this);
    // 初始化状态栏显示
    ui->label_6->setText("批次状态：未开始");  // 批次统计状态栏
    ui->label_7->setText("告警信息：无");      // 告警状态栏
    ui->label_7->setStyleSheet("color: red;");  // 告警文本设为红色


    // 初始化发送UDP的socket
    udpSendSocket = new QUdpSocket(this);

    ui->comboBoxProtocolListen->addItem("TCP", PacketReceiver::TCP);
    ui->comboBoxProtocolListen->addItem("UDP", PacketReceiver::UDP);
    ui->label_8->setText("接收统计：TCP 0 包，UDP 0 包");
    // 初始化 DelayInjector
    delayInjector = new DelayInjector(this);


    // 给delayModeCombo添加选项
    ui->delayModeCombo->addItem("无", DelayInjector::None);
    ui->delayModeCombo->addItem("卫星", DelayInjector::Satellite);
    ui->delayModeCombo->addItem("光纤", DelayInjector::Fiber);
    ui->delayModeCombo->addItem("5G", DelayInjector::FiveG);
    ui->distanceSpinBox->setRange(0, 100000);    // 设置最小值为1，最大值为100000
    ui->distanceSpinBox->setSingleStep(1);   // 设置步长为1（每次增减1）
    ui->distanceSpinBox->setValue(0);


    receiver = new PacketReceiver(this);
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::on_connectButton_clicked);
    connect(ui->listenButton, &QPushButton::clicked, this, &MainWindow::on_listenButton_clicked);
    connect(clientSocket, &QTcpSocket::connected, this, &MainWindow::on_connected);
    connect(clientSocket, &QTcpSocket::disconnected, this, &MainWindow::on_disconnected);
    connect(clientSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &MainWindow::on_errorOccurred);
    connect(server, &QTcpServer::newConnection, this, &MainWindow::on_newConnection);
    connect(ui->disconnectButton, &QPushButton::clicked, this, &MainWindow::on_disconnectButton_clicked);
    connect(ui->stopListenButton, &QPushButton::clicked, this, &MainWindow::on_stopListenButton_clicked);
    // 连接PacketReceiver的packetReceived信号
    connect(receiver, &PacketReceiver::packetReceived,
            this, &MainWindow::onPacketReceivedFromA);
    connect(receiver, &PacketReceiver::delayReceived, this, [=](double delay, const QString &proto)
            {
                if (proto == "TCP") {
                    tcpPacketCount++;
                } else if (proto == "UDP") {
                    udpPacketCount++;
                }
                // 生成统计文本
                QString statsText = QString("接收统计：TCP %1 包，UDP %2 包").arg(tcpPacketCount).arg(udpPacketCount);
                // 更新label_8的显示
                ui->label_8->setText(statsText);
            });
    connect(receiver, &PacketReceiver::logMessage,this, &MainWindow::onLogMessageReceived);
    logFlushTimer = new QTimer(this);
    logFlushTimer->setInterval(100);
    connect(logFlushTimer, &QTimer::timeout, this, [=]() {
        if (!logBuffer.isEmpty()) {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText(logBuffer.join("\n") + "\n");
            logBuffer.clear();
        }
    });
    logFlushTimer->start();
    connect(receiver, &PacketReceiver::delayAbnormal, this, [=](qint64 delay, const QString &proto) {
        logBuffer.append(QString("⚠️ %1 时延异常: %2 ms 超过阈值").arg(proto).arg(delay));
    });
    connect(receiver, &PacketReceiver::tcpPacketsProcessed, this, &MainWindow::onTcpPacketsProcessed);

    // 设置默认值和范围
    ui->spinBoxDelayThreshold->setMinimum(0);        // 最小0ms
    ui->spinBoxDelayThreshold->setMaximum(10000);    // 最大10秒
    ui->spinBoxDelayThreshold->setValue(1000);        // 默认1000ms
    // 设置初始值
    receiver->setAbnormalThreshold(ui->spinBoxDelayThreshold->value());
    // 动态绑定用户改动
    connect(ui->spinBoxDelayThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [=](double val) {  // 关键：将 int val 改为 double val
                receiver->setAbnormalThreshold(static_cast<qint64>(val * 1000));  // 额外注意：若阈值单位是纳秒，需转换（下文解释）
                ui->statusBrowser->moveCursor(QTextCursor::End);
                // arg(值, 最小宽度, 格式符, 小数位数)：'f' 表示固定格式，1 表示保留1位小数
                ui->statusBrowser->insertPlainText(QString("已更新异常时延阈值为：%1 ms\n").arg(val, 0, 'f', 1));
            });
    // 设置初始值和范围
    ui->spinBoxThresholdCount->setMinimum(1);
    ui->spinBoxThresholdCount->setMaximum(100);
    ui->spinBoxThresholdCount->setValue(3);  // 默认值
    // 动态设置 PacketReceiver 内部的触发阈值
    connect(ui->spinBoxThresholdCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [=](int val) {
                receiver->setAbnormalRepeatThreshold(val);
                ui->statusBrowser->moveCursor(QTextCursor::End);
                ui->statusBrowser->insertPlainText("已更新异常触发次数阈值为：" + QString::number(val) + " 次\n");
            });
    // 初始设置一次（防止未触发 valueChanged 时未生效）
    receiver->setAbnormalRepeatThreshold(ui->spinBoxThresholdCount->value());
    //清除日志按钮
    connect(ui->pushButtonClearLog, &QPushButton::clicked, this, [=]() {
        ui->statusBrowser->clear();
    });
    connect(receiver, &PacketReceiver::batchProcessingTime,
            this, &MainWindow::onBatchProcessingTime);
    // 在构造函数里给 TCP客户端绑定反馈接收
    connect(clientSocket, &QTcpSocket::readyRead, this, &MainWindow::onFeedbackReadyRead);

    // 连接延迟设置变化信号
    connect(ui->delayModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::on_delaySettingsChanged);
    connect(ui->distanceSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::on_delaySettingsChanged);
    receiver->setDelayInjector(delayInjector); // 将 DelayInjector 传递给 PacketReceiver
    qDebug() << "DelayInjector set for PacketReceiver";
}

MainWindow::~MainWindow() {
    //保险的断开处理
    if (serverClientSocket) {
        disconnect(serverClientSocket, nullptr, this, nullptr);
    }
    if (clientSocket) {
        disconnect(clientSocket, nullptr, this, nullptr);
    }
    if (receiver) {
        receiver->disconnect();  // 防止未处理完 signal
    }
    if (udpSocket) {
        udpSocket->close();
        udpSocket->deleteLater();
        udpSocket = nullptr;
    }
    if (udpSendSocket) {
        udpSendSocket->close();
        udpSendSocket->deleteLater();
        udpSendSocket = nullptr;
    }
    delete ui;
}


void MainWindow::on_listenButton_clicked() {
    quint16 port = ui->listenPortEdit->text().toUShort();
    QString protocol = ui->comboBoxProtocolListen->currentText();
    QString addrStr = ui->ipEdit->text();  // 比如 192.168.1.100
    QString sourceaddStr = ui->lineEdit->text();  // 比如 192.168.1.100
    quint16 forwardPort = ui->connectPortEdit->text().toUShort();  // 比如 12345
    QHostAddress forwardAddr(addrStr);
    ////设置UDP转发到C的ip和端口
    receiver->setForwardTarget(forwardAddr, forwardPort);
    forwardTargetAddress = forwardAddr;
    forwardTargetPort = forwardPort;

    if (protocol == "TCP") {
        // 如果UDP监听存在，关闭它
        if (udpSocket) {
            udpSocket->close();
            udpSocket->deleteLater();
            udpSocket = nullptr;
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("关闭UDP监听\n");
        }
        // 启动TCP监听
        if (server->isListening()) {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("TCP已在监听端口: " + QString::number(server->serverPort()) + "\n");
            return;
        }
        if (server->listen(QHostAddress::Any, port)) {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("TCP开始监听端口: " + QString::number(port) + "\n");
        } else {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("TCP监听失败: " + server->errorString() + "\n");
        }

    }
    //UDP端口绑定
    else if (protocol == "UDP") {
        // 如果TCP监听存在，关闭它
        if (server->isListening()) {
            server->close();
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("关闭TCP监听\n");
            if (clientSocket) {
                clientSocket->disconnectFromHost();
                clientSocket->deleteLater();
                clientSocket = nullptr;
                ui->statusBrowser->moveCursor(QTextCursor::End);
                ui->statusBrowser->insertPlainText("已断开与B的连接\n");
            }
        }

        // 启动UDP监听
        // 关闭已有UDP监听
        if (udpSocket) {
            udpSocket->close();
            udpSocket->deleteLater();
            udpSocket = nullptr;
        }
        udpSocket = new QUdpSocket(this);
        udpSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024);
        if (udpSocket->bind(QHostAddress::Any, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))  {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("UDP监听启动成功，端口：" + QString::number(port) + "\n");

            // 绑定readyRead信号，调用槽函数处理
            connect(udpSocket, &QUdpSocket::readyRead, this, &MainWindow::on_udpReadyRead);
            // 加这里监听UDP反馈
            connect(udpSendSocket, &QUdpSocket::readyRead, this, &MainWindow::onUdpFeedbackReadyRead);
        } else {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("UDP监听失败：" + udpSocket->errorString() + "\n");
            udpSocket->deleteLater();
            udpSocket = nullptr;
        }

    }
}

void MainWindow::on_newConnection() {
    //触发条件：监听端口收到来自 A 的连接
    //使用nextPendingConnection获取连接套接字serverClientSocket
    serverClientSocket = server->nextPendingConnection();
    ui->statusBrowser->moveCursor(QTextCursor::End);
    ui->statusBrowser->insertPlainText("收到来自A的连接\n");
    //监听 readyRead 信号，当有数据到达时会调用 on_readyRead
    connect(serverClientSocket, &QTcpSocket::readyRead, this, &MainWindow::on_readyRead);
}

void MainWindow::on_readyRead() {
    // 触发条件：A 发送数据给当前监听的 TCP 套接字
    if (!serverClientSocket) return;

    // 关键修复1：状态锁——防止函数执行期间被重复调用
    if (isProcessingTcpData) {
        qDebug() << "[on_readyRead] 跳过重复调用（处理中）";
        return;
    }
    isProcessingTcpData = true; // 标记为“正在处理”，阻止并行调用

    // 关键修复2：读取所有数据，并校验非空（避免空数据触发无效处理）
    QByteArray data = serverClientSocket->readAll();
    if (data.isEmpty()) {
        qDebug() << "[on_readyRead] 收到空数据，跳过处理";
        isProcessingTcpData = false; // 重置状态
        return;
    }

    // 仅处理非空数据（原有逻辑保留）
    qDebug() << "[on_readyRead] 有效TCP数据，大小=" << data.size();
    receiver->appendData(data);  // 调用PacketReceiver处理数据

    // 处理完成，重置状态锁
    isProcessingTcpData = false;
}

void MainWindow::on_udpReadyRead()
{
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        // 打印收到的UDP包的十六进制内容
        qDebug() << "UDP Header (first 4 bytes):" << datagram.left(4).toHex();
        //此处接收A发来的udp包
        receiver->appendUdpData(datagram);
    }
}

void MainWindow::on_connectButton_clicked() {
    QString ip = ui->ipEdit->text();
    quint16 port = ui->connectPortEdit->text().toUShort(); // 用于连接 C
    if (clientSocket->state() == QAbstractSocket::ConnectedState) {
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText("已经连接，无需重复连接。\n");
        return;
    }
    if (clientSocket->state() == QAbstractSocket::ConnectingState) {
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText("正在连接...\n");
        return;
    }
    clientSocket->connectToHost(ip, port);
    ui->statusBrowser->moveCursor(QTextCursor::End);
    ui->statusBrowser->insertPlainText("尝试连接到C: " + ip + ":" + QString::number(port) + "\n");
}

void MainWindow::on_disconnected() {
    ui->statusBrowser->moveCursor(QTextCursor::End);
    ui->statusBrowser->insertPlainText("已断开与C的连接\n");
}

bool hasConnected = false; // 在 MainWindow 类中声明这个变量

void MainWindow::on_connected() {
    hasConnected = true;
    ui->statusBrowser->moveCursor(QTextCursor::End);
    ui->statusBrowser->insertPlainText("已连接到C\n");
}

void MainWindow::on_errorOccurred(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    if (!hasConnected) {
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText("连接错误: " + clientSocket->errorString() + "\n");
    }
}

void MainWindow::on_disconnectButton_clicked() {
    if (clientSocket && clientSocket->state() == QAbstractSocket::ConnectedState) {
        clientSocket->disconnectFromHost();
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText("已断开与C的连接\n");
    } else {
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText("当前没有与其他主机的连接\n");
    }
}

//停止监听按钮
void MainWindow::on_stopListenButton_clicked() {
    QString proto = ui->comboBoxProtocolListen->currentText();

    if (proto == "TCP") {
        if (server->isListening()) {
            server->close();
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("已停止 TCP 监听\n");
            if (serverClientSocket) {
                serverClientSocket->disconnectFromHost();
                serverClientSocket->deleteLater();
                serverClientSocket = nullptr;
                ui->statusBrowser->moveCursor(QTextCursor::End);
                ui->statusBrowser->insertPlainText("已断开与 A 的连接\n");
            }
        } else {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("当前未处于 TCP 监听状态\n");
        }
    } else if (proto == "UDP") {
        if (udpSocket) {
            udpSocket->close();
            udpSocket->deleteLater();
            udpSocket = nullptr;
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("已停止 UDP 监听\n");
        } else {
            ui->statusBrowser->moveCursor(QTextCursor::End);
            ui->statusBrowser->insertPlainText("当前未处于 UDP 监听状态\n");
        }
    }
}

//转发给C
void MainWindow::onPacketReceivedFromA(const QByteArray &packet, quint8 proto)
{
    if (proto == 0x11)  // UDP
    {
        if (!udpSendSocket) {
            ui->statusBrowser->insertPlainText("UDP发送socket未初始化，转发失败\n");
            return;
        }
        // 从 receiver->setForwardTarget() 保存的目标地址和端口，这里用成员变量保存即可
        if (forwardTargetAddress.isNull() || forwardTargetPort == 0) {
            ui->statusBrowser->insertPlainText("未设置UDP转发目标，转发失败\n");
            return;
        }
        udpSendSocket->writeDatagram(packet, forwardTargetAddress, forwardTargetPort);
        ui->statusBrowser->insertPlainText("已转发UDP数据包给C\n");
    }
}



//在ui中添加UDP分片接收显示
void MainWindow::onLogMessageReceived(const QString &msg)
{
    ui->statusBrowser->insertPlainText(msg + "\n");
}

void MainWindow::onBatchProcessingTime(quint32 batchNumber, double timeMs) {
    batchProcessTimeMap[batchNumber] = timeMs;
    // 更新批次状态栏（label_6）
    QString batchText = QString("批次 %1 接收完毕，总耗时: %2 ms")
                            .arg(batchNumber)
                            .arg(timeMs, 0, 'f', 2);
    ui->label_6->setText(batchText);
}

// 新增槽函数：接收并解析 TCP反馈
QByteArray feedbackBuffer;

void MainWindow::onFeedbackReadyRead()
{
    QByteArray newData = clientSocket->readAll();
    feedbackBuffer.append(newData);

    qDebug() << "收到原始反馈数据:" << newData.toHex(' ').toUpper();

    QBuffer buffer(&feedbackBuffer);
    buffer.open(QIODevice::ReadOnly);
    QDataStream in(&buffer);
    in.setByteOrder(QDataStream::BigEndian);

    while (buffer.bytesAvailable() >= 12) {  // 12字节：magic(4) + batch(4) + durationMs(4)
        quint32 magic;
        quint32 batchNumber;
        quint32 durationInt;  // 先以整数读4字节

        in >> magic >> batchNumber >> durationInt;

        // 手动转换大端的整数为本地float
        durationInt = qFromBigEndian(durationInt);
        float durationMs;
        memcpy(&durationMs, &durationInt, sizeof(float));

        qDebug() << "收到的 durationMs:" << durationMs;
        qDebug() << "收到魔数 (magic):" << QString::number(magic, 16).toUpper().rightJustified(8, '0');

        if (magic == 0xFEEDC0DE) {
            ui->statusBrowser->insertPlainText(
                QString("[TCP反馈] 批次 %1, C处理耗时 %2 ms\n").arg(batchNumber).arg(durationMs, 0, 'f', 2));

            // 查找B端的耗时
            double bTimeMs = batchProcessTimeMap.value(batchNumber, -1);
            qDebug() << "查询B端耗时，batchNumber:" << batchNumber << "耗时(ms):" << bTimeMs;

            if (bTimeMs < 0) {
                ui->statusBrowser->insertPlainText(QString("批次 %1 的B端耗时未知，无法反馈给A\n").arg(batchNumber));
            } else {
                // B端耗时已知，再判断时延异常
                // 获取阈值设置
                int abnormalThreshold = ui->spinBoxDelayThreshold->value();
                int thresholdCount = ui->spinBoxThresholdCount->value();
                // TCP时延异常判断（严格条件：超过阈值且连续次数达标）
                if (bTimeMs > abnormalThreshold) {
                    consecutiveTcpAbnormalCount++;
                    // 仅当连续异常次数超过设定阈值时触发告警
                    if (consecutiveTcpAbnormalCount >= thresholdCount) {
                        QString alarmMsg = QString("🚨 UDP时延异常告警！时延(ms): %1 超过阈值: %2，连续异常次数已达 %3 次")
                                               .arg(bTimeMs, 0, 'f', 2)
                                               .arg(abnormalThreshold)
                                               .arg(consecutiveTcpAbnormalCount);
                        ui->label_7->setText(alarmMsg);  // 更新告警状态栏
                        ui->statusBrowser->insertPlainText(alarmMsg + "\n");
                    }
                }
                else {
                    // 时延正常时重置计数并清除告警
                    if (consecutiveTcpAbnormalCount > 0) {
                        ui->label_7->setText("告警信息：TCP时延恢复正常");
                        ui->statusBrowser->insertPlainText("✅ TCP时延恢复正常，连续异常次数已重置。\n");
                        consecutiveTcpAbnormalCount = 0;
                    }
                }
                // 组装反馈给A的包（同样用大端写入float）
                QByteArray feedbackToA;
                QDataStream out(&feedbackToA, QIODevice::WriteOnly);
                out.setByteOrder(QDataStream::BigEndian);

                out << (quint32)0xFEEDC0DE;
                out << batchNumber;

                // 转换 durationMs float -> 大端整数写入
                quint32 durationMsInt;
                memcpy(&durationMsInt, &durationMs, sizeof(float));
                durationMsInt = qToBigEndian(durationMsInt);
                out << durationMsInt;

                // 转换 bTimeMs float -> 大端整数写入
                float bTimeMs_f = static_cast<float>(bTimeMs);
                quint32 bTimeMsInt;
                memcpy(&bTimeMsInt, &bTimeMs_f, sizeof(float));
                bTimeMsInt = qToBigEndian(bTimeMsInt);
                out << bTimeMsInt;

                qDebug() << "反馈包大小:" << feedbackToA.size();  // 16字节
                qDebug() << "反馈包内容:" << feedbackToA.toHex(' ').toUpper();

                if (serverClientSocket && serverClientSocket->state() == QAbstractSocket::ConnectedState) {
                    qint64 written = serverClientSocket->write(feedbackToA);
                    if (written == feedbackToA.size()) {
                        ui->statusBrowser->insertPlainText(
                            QString("已将批次 %1 的反馈（含C耗时 %2 ms，B耗时 %3 ms）发送给A\n")
                                .arg(batchNumber).arg(durationMs, 0, 'f', 2).arg(bTimeMs, 0, 'f', 2));
                    } else {
                        ui->statusBrowser->insertPlainText(
                            QString("发送给A的反馈包写入失败，写入字节 %1 / %2\n").arg(written).arg(feedbackToA.size()));
                    }
                    serverClientSocket->flush();
                } else {
                    ui->statusBrowser->insertPlainText("未检测到与A的连接，无法发送反馈\n");
                }
            }

            feedbackBuffer.remove(0, buffer.pos());
            buffer.seek(0);
            in.device()->seek(0);
        } else {
            ui->statusBrowser->insertPlainText("[TCP反馈] 非法反馈包，魔数不匹配\n");
            feedbackBuffer.remove(0, 1);
            buffer.seek(0);
            in.device()->seek(0);
        }
    }
}

// 新增槽函数：接收并解析 UDP反馈
void MainWindow::onUdpFeedbackReadyRead()
{
    qDebug() << "B端UDP反馈 readyRead 信号触发";

    while (udpSendSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpSendSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        udpSendSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        qDebug() << "[Sender] 收到来自 C 的反馈：" << datagram.toHex(' ').toUpper();

        if (datagram.size() < 12) {
            ui->statusBrowser->insertPlainText("[UDP反馈] 数据包过小，丢弃\n");
            continue;
        }

        QBuffer buffer(&datagram);
        buffer.open(QIODevice::ReadOnly);
        QDataStream in(&buffer);
        in.setByteOrder(QDataStream::BigEndian);

        quint32 magic;
        quint32 batchNumber;
        quint32 durationInt;

        in >> magic >> batchNumber >> durationInt;

        // 手动将大端的整数转换为本地 float
        durationInt = qFromBigEndian(durationInt);
        float durationMs;
        memcpy(&durationMs, &durationInt, sizeof(float));

        if (magic == 0xFEEDC0DE) {
            ui->statusBrowser->insertPlainText(
                QString("[UDP反馈] 批次 %1, 处理耗时 %2 ms 来源 %3:%4\n")
                    .arg(batchNumber)
                    .arg(durationMs, 0, 'f', 2)
                    .arg(sender.toString())
                    .arg(senderPort)
                );
            // 查找B端的耗时
            double bTimeMs = batchProcessTimeMap.value(batchNumber, -1);
            qDebug() << "查询B端耗时，batchNumber:" << batchNumber << "耗时(ms):" << bTimeMs;

            if (bTimeMs < 0) {
                ui->statusBrowser->insertPlainText(QString("批次 %1 的B端耗时未知，无法反馈给A\n").arg(batchNumber));
            } else {
                // B端耗时已知，再判断时延异常
                // 获取阈值设置
                int abnormalThreshold = ui->spinBoxDelayThreshold->value();
                int thresholdCount = ui->spinBoxThresholdCount->value();
                // TCP时延异常判断（严格条件：超过阈值且连续次数达标）
                if (bTimeMs > abnormalThreshold) {
                    consecutiveTcpAbnormalCount++;
                    // 仅当连续异常次数超过设定阈值时触发告警
                    if (consecutiveTcpAbnormalCount >= thresholdCount) {
                        QString alarmMsg = QString("🚨 UDP时延异常告警！时延(ms): %1 超过阈值: %2，连续异常次数已达 %3 次")
                                               .arg(bTimeMs, 0, 'f', 2)
                                               .arg(abnormalThreshold)
                                               .arg(consecutiveTcpAbnormalCount);
                        ui->label_7->setText(alarmMsg);  // 更新告警状态栏
                        ui->statusBrowser->insertPlainText(alarmMsg + "\n");
                    }
                }
                else {
                    // 时延正常时重置计数并清除告警
                    if (consecutiveTcpAbnormalCount > 0) {
                        ui->label_7->setText("告警信息：TCP时延恢复正常");
                        ui->statusBrowser->insertPlainText("✅ TCP时延恢复正常，连续异常次数已重置。\n");
                        consecutiveTcpAbnormalCount = 0;
                    }
                }
                // 组装反馈给A的包（同样用大端写入float）
                QByteArray feedbackToA;
                QDataStream out(&feedbackToA, QIODevice::WriteOnly);
                out.setByteOrder(QDataStream::BigEndian);

                out << (quint32)0xFEEDC0DE;
                out << batchNumber;

                // 转换 durationMs float -> 大端整数写入
                quint32 durationMsInt;
                memcpy(&durationMsInt, &durationMs, sizeof(float));
                durationMsInt = qToBigEndian(durationMsInt);
                out << durationMsInt;

                // 转换 bTimeMs float -> 大端整数写入
                float bTimeMs_f = static_cast<float>(bTimeMs);
                quint32 bTimeMsInt;
                memcpy(&bTimeMsInt, &bTimeMs_f, sizeof(float));
                bTimeMsInt = qToBigEndian(bTimeMsInt);
                out << bTimeMsInt;

                qDebug() << "反馈包大小:" << feedbackToA.size();  // 16字节
                qDebug() << "反馈包内容:" << feedbackToA.toHex(' ').toUpper();
                QString ipStr = ui->lineEdit->text();      // A 的 IP 地址输入框
                QString portStr = ui->listenPortEdit->text();  // A 的端口号输入框
                QString udpfeedbackportStr = ui->udpfeedbackPort->text();
                QHostAddress aAddress(ipStr);
                quint16 aPort = static_cast<quint16>(portStr.toUShort());
                quint16 udpfeedbackportStr1 = static_cast<quint16>(udpfeedbackportStr.toUShort());

                if (udpSocket) {
                    qint64 written = udpSocket->writeDatagram(feedbackToA, aAddress, udpfeedbackportStr1);
                    if (written == feedbackToA.size()) {
                        ui->statusBrowser->insertPlainText(
                            QString("已将批次 %1 的反馈（含C耗时 %2 ms，B耗时 %3 ms）发送给A\n")
                                .arg(batchNumber).arg(durationMs, 0, 'f', 2).arg(bTimeMs, 0, 'f', 2));
                    } else {
                        ui->statusBrowser->insertPlainText(
                            QString("发送给A的反馈包写入失败，写入字节 %1 / %2\n").arg(written).arg(feedbackToA.size()));
                    }
                    // udpSocket不需要flush
                } else {
                    ui->statusBrowser->insertPlainText("未检测到与A的连接，无法发送反馈\n");
                }
            }
            feedbackBuffer.remove(0, buffer.pos());
            buffer.seek(0);
            in.device()->seek(0);
        } else {
            ui->statusBrowser->insertPlainText("[UDP反馈] 非法反馈包，魔数错误\n");
        }
    }
}

void MainWindow::onTcpPacketsProcessed(const QList<QByteArray> &packets)
{
    ui->statusBrowser->insertPlainText(QString("处理完成 %1 个TCP数据包，准备转发\n").arg(packets.size()));

    if (!clientSocket || clientSocket->state() != QAbstractSocket::ConnectedState) {
        ui->statusBrowser->insertPlainText("未连接到C，TCP转发失败\n");
        return;
    }

    for (const QByteArray &packet : packets) {
        if (packet.size() < 8) {
            ui->statusBrowser->insertPlainText("收到的TCP数据包过小，丢弃\n");
            continue;
        }

        QByteArray magic = packet.left(2);
        if (magic != QByteArray::fromHex("aa55")) {
            ui->statusBrowser->insertPlainText("收到的TCP包Magic Header非法，丢弃\n");
            continue;
        }

        // 先写4字节包长（大端）
        quint32 packetSize = static_cast<quint32>(packet.size());
        QByteArray lengthBytes;
        QDataStream lengthStream(&lengthBytes, QIODevice::WriteOnly);
        lengthStream.setByteOrder(QDataStream::BigEndian);
        lengthStream << packetSize;

        qint64 writtenLen1 = clientSocket->write(lengthBytes);
        if (writtenLen1 != lengthBytes.size()) {
            ui->statusBrowser->insertPlainText(QString("写入TCP包长失败（写入字节 %1/%2）\n").arg(writtenLen1).arg(lengthBytes.size()));
            continue;
        }

        // 再写包体
        qint64 writtenLen2 = clientSocket->write(packet);
        if (writtenLen2 != packet.size()) {
            ui->statusBrowser->insertPlainText(QString("写入TCP包体失败（写入字节 %1/%2）\n").arg(writtenLen2).arg(packet.size()));
            continue;
        }

        clientSocket->flush();
        ui->statusBrowser->insertPlainText("已转发TCP数据包给C\n");

        // 触发 packetReceived 信号以保持原有逻辑
        emit receiver->packetReceived(packet, 0x06);
    }
}

void MainWindow::on_delaySettingsChanged()
{
    QComboBox *delayModeCombo = qobject_cast<QComboBox*>(sender());
    QSpinBox *distanceSpinBox = qobject_cast<QSpinBox*>(sender());
    if (delayModeCombo) {
        DelayInjector::DelayMode mode = static_cast<DelayInjector::DelayMode>(delayModeCombo->currentData().toInt());
        delayInjector->setMode(mode);
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText(QString("已更新延迟模式为：%1\n").arg(delayModeCombo->currentText()));
    }
    if (distanceSpinBox) {
        DelayInjector::Distance distance = static_cast<DelayInjector::Distance>(distanceSpinBox->value());
        delayInjector->setDistance(distance);
        ui->statusBrowser->moveCursor(QTextCursor::End);
        ui->statusBrowser->insertPlainText(QString("已更新距离为：%1 公里\n").arg(distanceSpinBox->value()));
    }
}
