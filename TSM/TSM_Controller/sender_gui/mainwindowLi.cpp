#include "mainwindowLi.h"
#include "ui_mainwindowLi.h"
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGridLayout>
#include <QPainter>
#include <algorithm>
#include <cmath>

// DelayStatsDialogLi 实现
DelayStatsDialogLi::DelayStatsDialogLi(QWidget *parent) : QDialog(parent) {
    setWindowTitle("时延统计分析");
    setMinimumSize(800, 600);

    m_aPercentLabel = new QLabel("A端: --%");
    m_bPercentLabel = new QLabel("B端: --%");
    m_cPercentLabel = new QLabel("C端: --%");

    m_aP50Label = new QLabel("--");
    m_aP90Label = new QLabel("--");
    m_aP99Label = new QLabel("--");

    m_bP50Label = new QLabel("--");
    m_bP90Label = new QLabel("--");
    m_bP99Label = new QLabel("--");

    m_cP50Label = new QLabel("--");
    m_cP90Label = new QLabel("--");
    m_cP99Label = new QLabel("--");

    m_aStdLabel = new QLabel("A端: --");
    m_bStdLabel = new QLabel("B端: --");
    m_cStdLabel = new QLabel("C端: --");

    m_aHistogram = new HistogramWidget(this);
    m_bHistogram = new HistogramWidget(this);
    m_cHistogram = new HistogramWidget(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 占比显示区域
    QGroupBox *percentGroup = new QGroupBox("各模块时延占比");
    QHBoxLayout *percentLayout = new QHBoxLayout;
    percentLayout->addWidget(m_aPercentLabel);
    percentLayout->addWidget(m_bPercentLabel);
    percentLayout->addWidget(m_cPercentLabel);
    percentGroup->setLayout(percentLayout);
    mainLayout->addWidget(percentGroup);

    // 百分位显示区域
    QGroupBox *percentileGroup = new QGroupBox("百分位分析 (ms)");
    QGridLayout *percentileLayout = new QGridLayout;
    percentileLayout->addWidget(new QLabel(""), 0, 1);
    percentileLayout->addWidget(new QLabel("P50"), 0, 2);
    percentileLayout->addWidget(new QLabel("P90"), 0, 3);
    percentileLayout->addWidget(new QLabel("P99"), 0, 4);
    percentileLayout->addWidget(new QLabel("A端:"), 1, 1);
    percentileLayout->addWidget(m_aP50Label, 1, 2);
    percentileLayout->addWidget(m_aP90Label, 1, 3);
    percentileLayout->addWidget(m_aP99Label, 1, 4);
    percentileLayout->addWidget(new QLabel("B端:"), 2, 1);
    percentileLayout->addWidget(m_bP50Label, 2, 2);
    percentileLayout->addWidget(m_bP90Label, 2, 3);
    percentileLayout->addWidget(m_bP99Label, 2, 4);
    percentileLayout->addWidget(new QLabel("C端:"), 3, 1);
    percentileLayout->addWidget(m_cP50Label, 3, 2);
    percentileLayout->addWidget(m_cP90Label, 3, 3);
    percentileLayout->addWidget(m_cP99Label, 3, 4);
    percentileGroup->setLayout(percentileLayout);
    mainLayout->addWidget(percentileGroup);

    // 直方图区域
    QGroupBox *histGroup = new QGroupBox("时延分布直方图");
    QVBoxLayout *histLayout = new QVBoxLayout;
    histLayout->addWidget(new QLabel("A端时延分布"));
    histLayout->addWidget(m_aHistogram);
    histLayout->addWidget(new QLabel("B端时延分布"));
    histLayout->addWidget(m_bHistogram);
    histLayout->addWidget(new QLabel("C端时延分布"));
    histLayout->addWidget(m_cHistogram);
    histGroup->setLayout(histLayout);
    mainLayout->addWidget(histGroup);

    // 标准差区域
    QGroupBox *stdGroup = new QGroupBox("标准差 (ms)");
    QHBoxLayout *stdLayout = new QHBoxLayout;
    stdLayout->addWidget(m_aStdLabel);
    stdLayout->addWidget(m_bStdLabel);
    stdLayout->addWidget(m_cStdLabel);
    stdGroup->setLayout(stdLayout);
    mainLayout->addWidget(stdGroup);

    // 清除按钮
    QPushButton *clearButton = new QPushButton("清除统计数据", this);
    connect(clearButton, &QPushButton::clicked, this, &DelayStatsDialogLi::clearStats);
    mainLayout->addWidget(clearButton);
}

void DelayStatsDialogLi::updateStats(double a, double b, double c) {
    m_aDurations.append(a);
    m_bDurations.append(b);
    m_cDurations.append(c);

    double total = a + b + c;
    if (total > 0) {
        double aPercent = (a / total) * 100.0;
        double bPercent = (b / total) * 100.0;
        double cPercent = (c / total) * 100.0;
        m_aPercentLabel->setText(QString("A端: %1%").arg(aPercent, 0, 'f', 1));
        m_bPercentLabel->setText(QString("B端: %1%").arg(bPercent, 0, 'f', 1));
        m_cPercentLabel->setText(QString("C端: %1%").arg(cPercent, 0, 'f', 1));
    } else {
        m_aPercentLabel->setText("A端: 0.0%");
        m_bPercentLabel->setText("B端: 0.0%");
        m_cPercentLabel->setText("C端: 0.0%");
    }

    QVector<double> aData = m_aDurations;
    QVector<double> bData = m_bDurations;
    QVector<double> cData = m_cDurations;

    std::sort(aData.begin(), aData.end());
    std::sort(bData.begin(), bData.end());
    std::sort(cData.begin(), cData.end());

    if (!aData.isEmpty()) {
        m_aP50Label->setText(QString::number(calculatePercentile(aData, 50), 'f', 2));
        m_aP90Label->setText(QString::number(calculatePercentile(aData, 90), 'f', 2));
        m_aP99Label->setText(QString::number(calculatePercentile(aData, 99), 'f', 2));
        m_aStdLabel->setText(QString("A端: %1").arg(QString::number(calculateStdDev(aData), 'f', 2)));
    }

    if (!bData.isEmpty()) {
        m_bP50Label->setText(QString::number(calculatePercentile(bData, 50), 'f', 2));
        m_bP90Label->setText(QString::number(calculatePercentile(bData, 90), 'f', 2));
        m_bP99Label->setText(QString::number(calculatePercentile(bData, 99), 'f', 2));
        m_bStdLabel->setText(QString("B端: %1").arg(QString::number(calculateStdDev(bData), 'f', 2)));
    }

    if (!cData.isEmpty()) {
        m_cP50Label->setText(QString::number(calculatePercentile(cData, 50), 'f', 2));
        m_cP90Label->setText(QString::number(calculatePercentile(cData, 90), 'f', 2));
        m_cP99Label->setText(QString::number(calculatePercentile(cData, 99), 'f', 2));
        m_cStdLabel->setText(QString("C端: %1").arg(QString::number(calculateStdDev(cData), 'f', 2)));
    }

    static_cast<HistogramWidget*>(m_aHistogram)->setData(aData);
    static_cast<HistogramWidget*>(m_bHistogram)->setData(bData);
    static_cast<HistogramWidget*>(m_cHistogram)->setData(cData);
}

void DelayStatsDialogLi::clearStats() {
    m_aDurations.clear();
    m_bDurations.clear();
    m_cDurations.clear();

    m_aPercentLabel->setText("A端: --%");
    m_bPercentLabel->setText("B端: --%");
    m_cPercentLabel->setText("C端: --%");

    m_aP50Label->setText("--");
    m_aP90Label->setText("--");
    m_aP99Label->setText("--");

    m_bP50Label->setText("--");
    m_bP90Label->setText("--");
    m_bP99Label->setText("--");

    m_cP50Label->setText("--");
    m_cP90Label->setText("--");
    m_cP99Label->setText("--");

    m_aStdLabel->setText("A端: --");
    m_bStdLabel->setText("B端: --");
    m_cStdLabel->setText("C端: --");

    static_cast<HistogramWidget*>(m_aHistogram)->setData(QVector<double>());
    static_cast<HistogramWidget*>(m_bHistogram)->setData(QVector<double>());
    static_cast<HistogramWidget*>(m_cHistogram)->setData(QVector<double>());
}

double DelayStatsDialogLi::calculatePercentile(QVector<double> &sortedData, double percentile) {
    if (sortedData.isEmpty()) return 0;
    double index = (percentile / 100.0) * (sortedData.size() - 1);
    int lower = floor(index);
    int upper = ceil(index);
    double weight = index - lower;
    return sortedData[lower] * (1 - weight) + sortedData[upper] * weight;
}

double DelayStatsDialogLi::calculateStdDev(QVector<double> &data) {
    if (data.size() < 2) return 0;
    double mean = 0;
    for (double val : data) mean += val;
    mean /= data.size();

    double variance = 0;
    for (double val : data) variance += pow(val - mean, 2);
    variance /= data.size() - 1;
    return sqrt(variance);
}

void DelayStatsDialogLi::updateHistograms() {
    QVector<double> aData = m_aDurations;
    QVector<double> bData = m_bDurations;
    QVector<double> cData = m_cDurations;

    std::sort(aData.begin(), aData.end());
    std::sort(bData.begin(), bData.end());
    std::sort(cData.begin(), cData.end());

    static_cast<HistogramWidget*>(m_aHistogram)->setData(aData);
    static_cast<HistogramWidget*>(m_bHistogram)->setData(bData);
    static_cast<HistogramWidget*>(m_cHistogram)->setData(cData);
}

void DelayStatsDialogLi::paintEvent(QPaintEvent *event) {
    QDialog::paintEvent(event);
}




// MainWindowLi 实现
MainWindowLi::MainWindowLi(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindowLi),
    worker(nullptr),
    workerThread(nullptr),
    m_statsDialog(new DelayStatsDialogLi(this)),
    bgThread_(nullptr)

{
    ui->setupUi(this);
    chartTabs = ui->chartTabs;

    ui->portSpin->setRange(1, 65535);
    ui->portSpin->setValue(8080);
    ui->retrySpin->setRange(0, 10);
    ui->retrySpin->setValue(3);

    ui->protocolCombo->addItems({"TCP"});
    ui->bandwidthCombo->addItems({"0 MBps", "1 MBps", "10 MBps", "100 MBps", "1000 MBps"});

    // 添加统计分析按钮
    QPushButton *statsButton = new QPushButton("时延统计分析", this);
    ui->buttonLayout->addWidget(statsButton); // 假设 UI 中有 buttonLayout
    connect(statsButton, &QPushButton::clicked, this, &MainWindowLi::onStatsButtonClicked);

    connect(ui->startButton, &QPushButton::clicked, this, &MainWindowLi::onStartClicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindowLi::onStopClicked);
    connect(ui->clearChartButton, &QPushButton::clicked, this, &MainWindowLi::onClearChartClicked);
    connect(ui->instantProbeButton, &QPushButton::clicked, this, &MainWindowLi::onInstantProbeClicked);
    connect(ui->startBgButton, &QPushButton::clicked, this, &MainWindowLi::startBgButton);
    connect(ui->stopBgButton,  &QPushButton::clicked, this, &MainWindowLi::stopBgButton);
    // 连接 spinBoxDelayThreshold 的 valueChanged 信号
    connect(ui->spinBoxDelayThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindowLi::onDelayThresholdChanged);
}

MainWindowLi::~MainWindowLi() {
    if (workerThread) {
        worker->stopTest();
        workerThread->quit();
        workerThread->wait();
    }
    delete ui;
}

void MainWindowLi::onStartClicked() {
    if (workerThread) return;

    QString ip = ui->ipEdit->text();
    int port = ui->portSpin->value();
    int pktSize = ui->sizeLineEdit->text().toInt();
    bool useUDP = ui->protocolCombo->currentText() == "UDP"; // 修正：TCP 应为 !useUDP
    int mbps = ui->bandwidthCombo->currentText().split(' ').first().toInt();
    bool simRT = ui->retransmitCheck->isChecked();

    workerThread = new QThread(this);
    worker = new NetworkWorker;
    worker->moveToThread(workerThread);

    connect(workerThread, &QThread::started, [=]() {
        worker->setSimulateRetransmission(simRT);
        worker->startTest(ip, port, pktSize, mbps, useUDP);
    });
    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &NetworkWorker::rttMeasured, this, &MainWindowLi::onRTTMeasured);
    connect(worker, &NetworkWorker::statusUpdate, this, &MainWindowLi::onStatusUpdate);
    connect(worker, &NetworkWorker::rttAlarm, this, [=](const QString &msg) {
        ui->label->setText(msg);
        ui->label->setStyleSheet("color: red;");
    });
    connect(worker, &NetworkWorker::rttRecovered, this, [=](const QString &msg) {
        ui->label->setText(msg);
        ui->label->setStyleSheet("color: green;");
    });

    // 新增：连接统计信号
    connect(worker, &NetworkWorker::delayStatsUpdated, m_statsDialog, &DelayStatsDialogLi::updateStats);

    worker->setDelayThreshold(ui->spinBoxDelayThreshold->value());
    worker->setAbnormalCountThreshold(ui->spinBoxThresholdCount->value());

    workerThread->start();
    ui->statusLabel->setText("测试已启动");
}

void MainWindowLi::onStopClicked() {
    if (!workerThread) return;

    QMetaObject::invokeMethod(worker, "stopTest", Qt::QueuedConnection);
    workerThread->quit();
    workerThread->wait();
    workerThread->deleteLater();
    workerThread = nullptr;
    worker = nullptr;

    ui->statusLabel->setText("已停止");
}

void MainWindowLi::onClearChartClicked() {
    chartMap.clear();
    seqMap.clear();
    chartTabs->clear();
}

void MainWindowLi::onInstantProbeClicked() {
    if (!worker) {
        ui->statusLabel->setText("尚未启动测试");
        return;
    }
    QMetaObject::invokeMethod(worker, "sendProbe", Qt::QueuedConnection);
    ui->statusLabel->setText("正在测量瞬时RTT...");
}

void MainWindowLi::onRTTMeasured(double rtt, int bgMbps, bool isRetransmit) {
    if (!chartMap.contains(bgMbps)) {
        auto *chart = new SimplePlotWidget;
        chartMap.insert(bgMbps, chart);
        chartTabs->addTab(chart, QString("%1 MBps").arg(bgMbps));
        seqMap.insert(bgMbps, 0);
    }
    int x = seqMap[bgMbps];
    seqMap[bgMbps] = x + 1;
    chartMap[bgMbps]->addPoint(x, rtt, isRetransmit);
}

void MainWindowLi::onStatusUpdate(const QString &msg) {
    ui->statusLabel->setText(msg);
}

void MainWindowLi::onRTTAlarm(const QString &alarmMsg) {
    ui->label->setText(alarmMsg);
    qDebug() << "告警：" << alarmMsg;
}

void MainWindowLi::onRTTRecovered(const QString &recoverMsg) {
    ui->label->setText(recoverMsg);
    qDebug() << "恢复：" << recoverMsg;
}

void MainWindowLi::onStatsButtonClicked() {
    if (m_statsDialog->isHidden()) {
        m_statsDialog->show();
    } else {
        m_statsDialog->raise();
    }
}

void MainWindowLi::startBgButton()
{
    if (!bgThread_) {
        bgThread_ = new QThread(this);
        bgWorker_ = new BackgroundSender;
        bgWorker_->moveToThread(bgThread_);

        connect(bgThread_, &QThread::finished, bgWorker_, &QObject::deleteLater);
        bgThread_->start();
    }

    if (!bgWorker_) return;

    QString ip = ui->ipEdit->text();
    quint16 port = ui->portSpin->value();
    int pktSize = ui->sizeLineEdit->text().toInt();
    int mbps = ui->bandwidthCombo->currentText().split(' ').first().toInt();

    QMetaObject::invokeMethod(bgWorker_, "start", Qt::QueuedConnection,
                              Q_ARG(QString, ip),
                              Q_ARG(quint16, port),
                              Q_ARG(int, pktSize),
                              Q_ARG(int, mbps));

    ui->statusLabel->setText("🟢 背景 UDP 流量已启动");
}

void MainWindowLi::stopBgButton()
{
    if (bgWorker_) {
        QMetaObject::invokeMethod(bgWorker_, "stop", Qt::QueuedConnection);
    }

    if (bgThread_) {
        bgThread_->quit();
        bgThread_->wait();
        bgThread_->deleteLater();
        bgThread_ = nullptr;
    }

    bgWorker_ = nullptr;
    ui->statusLabel->setText("🛑 背景 UDP 流量已停止");
}
void MainWindowLi::onDelayThresholdChanged(double threshold) {
    if (worker) {
        worker->setDelayThreshold(threshold);
        ui->statusLabel->setText(QString("已更新时延阈值: %1 ms").arg(threshold, 0, 'f', 1));
        qDebug() << "设置 NetworkWorker::abnormalThreshold_ 为:" << threshold << "ms";
    }
}
