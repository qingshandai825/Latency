#include "TSMmainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextEdit>
#include <QFileDialog>
#include <QHostAddress>
#include <QPalette>
#include <QFrame>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <QGroupBox>
#include <QDebug>

// 自定义直方图控件
class HistogramWidget : public QWidget {
public:
    explicit HistogramWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(100);
        setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    }

    void setData(const QVector<double> &data) {
        m_data = data;
        update(); // 触发重绘
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);

        if (m_data.isEmpty()) return;

        // 计算数据范围
        double minVal = *std::min_element(m_data.begin(), m_data.end());
        double maxVal = *std::max_element(m_data.begin(), m_data.end());
        if (minVal == maxVal) {
            // 所有值相同，绘制一条水平线
            painter.drawLine(10, height()/2, width()-10, height()/2);
            return;
        }

        // 分箱数量
        const int bins = 10;
        int binCounts[bins] = {0};

        // 统计每个区间的数量
        for (double val : m_data) {
            int bin = static_cast<int>((val - minVal) / (maxVal - minVal) * bins);
            if (bin >= bins) bin = bins - 1;
            if (bin < 0) bin = 0;
            binCounts[bin]++;
        }

        // 找到最大计数（用于归一化高度）
        int maxCount = *std::max_element(binCounts, binCounts + bins);
        if (maxCount == 0) return;

        // 绘制坐标轴
        int margin = 20;
        QRect rect = this->rect().adjusted(margin, margin, -margin, -margin);
        int width = rect.width();
        int height = rect.height();

        // 绘制X轴
        painter.drawLine(rect.bottomLeft(), rect.bottomRight());
        // 绘制Y轴
        painter.drawLine(rect.bottomLeft(), rect.topLeft());

        // 绘制直方图柱形
        int binWidth = width / bins;
        for (int i = 0; i < bins; i++) {
            int barHeight = static_cast<int>((binCounts[i] * 1.0 / maxCount) * height);
            QRect barRect(
                rect.left() + i * binWidth,
                rect.bottom() - barHeight,
                binWidth - 2, // 减2是为了柱形之间留空隙
                barHeight
                );
            painter.fillRect(barRect, QColor(0, 120, 215));
            painter.drawRect(barRect);
        }

        // 绘制X轴标签
        painter.drawText(rect.bottomLeft().x() + width/2 - 40, rect.bottom() + margin,
                         QString("范围: %1 - %2").arg(minVal, 0, 'f', 1).arg(maxVal, 0, 'f', 1));
    }

private:
    QVector<double> m_data;
};

// 时延统计对话框实现
DelayStatsDialog::DelayStatsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("时延统计分析");
    setMinimumSize(800, 600);

    // 初始化所有标签
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

    // 添加清除按钮
    QPushButton *clearButton = new QPushButton("清除统计数据", this);
    connect(clearButton, &QPushButton::clicked, this, &DelayStatsDialog::clearStats);
    mainLayout->addWidget(clearButton);
}

void DelayStatsDialog::updateStats(qint64 a, float b, float c) {
    // 存储新数据
    m_aDurations.append(a);
    m_bDurations.append(b);
    m_cDurations.append(c);

    // 计算占比
    double total = a + b + c;
    if (total > 0) {
        double aPercent = (static_cast<double>(a) / total) * 100.0;
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

    // 转换为double向量用于计算
    QVector<double> aData;
    for (auto val : m_aDurations) aData.append(static_cast<double>(val));

    QVector<double> bData;
    for (auto val : m_bDurations) bData.append(static_cast<double>(val));

    QVector<double> cData;
    for (auto val : m_cDurations) cData.append(static_cast<double>(val));

    // 排序数据
    std::sort(aData.begin(), aData.end());
    std::sort(bData.begin(), bData.end());
    std::sort(cData.begin(), cData.end());

    // 计算百分位值
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

    // 更新直方图数据
    static_cast<HistogramWidget*>(m_aHistogram)->setData(aData);
    static_cast<HistogramWidget*>(m_bHistogram)->setData(bData);
    static_cast<HistogramWidget*>(m_cHistogram)->setData(cData);
}

void DelayStatsDialog::updateHistograms() {
    // 转换为double向量用于绘制
    QVector<double> aData;
    for (auto val : m_aDurations) aData.append(static_cast<double>(val));

    QVector<double> bData;
    for (auto val : m_bDurations) bData.append(static_cast<double>(val));

    QVector<double> cData;
    for (auto val : m_cDurations) cData.append(static_cast<double>(val));

    // 排序数据
    std::sort(aData.begin(), aData.end());
    std::sort(bData.begin(), bData.end());
    std::sort(cData.begin(), cData.end());

    // 更新直方图数据
    static_cast<HistogramWidget*>(m_aHistogram)->setData(aData);
    static_cast<HistogramWidget*>(m_bHistogram)->setData(bData);
    static_cast<HistogramWidget*>(m_cHistogram)->setData(cData);
}

void DelayStatsDialog::clearStats() {
    m_aDurations.clear();
    m_bDurations.clear();
    m_cDurations.clear();

    // 重置所有显示
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

    // 清除直方图
    static_cast<HistogramWidget*>(m_aHistogram)->setData(QVector<double>());
    static_cast<HistogramWidget*>(m_bHistogram)->setData(QVector<double>());
    static_cast<HistogramWidget*>(m_cHistogram)->setData(QVector<double>());
}

double DelayStatsDialog::calculatePercentile(QVector<double> &sortedData, double percentile) {
    if (sortedData.isEmpty()) return 0;
    double index = (percentile / 100.0) * (sortedData.size() - 1);
    int lower = floor(index);
    int upper = ceil(index);
    double weight = index - lower;
    return sortedData[lower] * (1 - weight) + sortedData[upper] * weight;
}

double DelayStatsDialog::calculateStdDev(QVector<double> &data) {
    if (data.size() < 2) return 0;
    double mean = 0;
    for (double val : data) mean += val;
    mean /= data.size();

    double variance = 0;
    for (double val : data) variance += pow(val - mean, 2);
    variance /= data.size() - 1;

    return sqrt(variance);
}

void DelayStatsDialog::paintEvent(QPaintEvent *event) {
    QDialog::paintEvent(event);
}

// 主窗口实现
TSMMainWindow::TSMMainWindow(QWidget *parent) : QWidget(parent) {
    m_controller = new TSM_Controller(this);
    m_statsDialog = new DelayStatsDialog(this);
    setWindowTitle("发送端");

    // 主布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 异常阈值设置区域
    QHBoxLayout *thresholdLayout = new QHBoxLayout;

    // 异常时延阈值（ms）
    m_delayThresholdSpin = new QDoubleSpinBox(this);
    m_delayThresholdSpin->setRange(0.1, 10000);  // 0.1ms ~ 10秒
    m_delayThresholdSpin->setValue(1000);       // 默认1000ms
    thresholdLayout->addWidget(new QLabel("异常处理时延阈值:"));
    thresholdLayout->addWidget(m_delayThresholdSpin);
    thresholdLayout->addSpacing(20);

    // 阈值超限次数
    m_abnormalCountSpin = new QSpinBox(this);
    m_abnormalCountSpin->setRange(1, 10);      // 1~10次
    m_abnormalCountSpin->setValue(3);          // 默认3次
    thresholdLayout->addWidget(new QLabel("超过阈值次数:"));
    thresholdLayout->addWidget(m_abnormalCountSpin);

    mainLayout->addLayout(thresholdLayout);

    // 组播模式说明
    QLabel *hintLabel = new QLabel(this);
    hintLabel->setText("组播模式说明:\n"
                       "1. 所有接收端需要加入相同的组播组\n"
                       "2. 接收端应绑定到相同的端口\n"
                       "3. 示例组播地址: 239.255.0.1");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(hintLabel);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({"单播", "多播", "广播"});

    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItems({"UDP", "TCP"});
    m_protocolCombo->setEnabled(false);

    // 调度算法和优先级分布选择
    m_schedulingAlgoCombo = new QComboBox(this);
    m_schedulingAlgoCombo->addItems({"先进先出队列", "加权公平队列"});
    m_priorityDistCombo = new QComboBox(this);
    m_priorityDistCombo->addItems({"均匀分布", "偏斜分布"});

    QHBoxLayout *typeLayout = new QHBoxLayout;
    typeLayout->addWidget(new QLabel("类型:"));
    typeLayout->addWidget(m_protocolCombo);
    typeLayout->addWidget(m_typeCombo);
    typeLayout->addWidget(new QLabel("调度算法:"));
    typeLayout->addWidget(m_schedulingAlgoCombo);
    typeLayout->addWidget(new QLabel("优先级分布:"));
    typeLayout->addWidget(m_priorityDistCombo);
    mainLayout->addLayout(typeLayout);

    // 反馈端口输入
    QHBoxLayout *feedbackPortLayout = new QHBoxLayout;
    m_feedbackPortSpin = new QSpinBox(this);
    m_feedbackPortSpin->setRange(1, 65535);
    m_feedbackPortSpin->setValue(5001);
    feedbackPortLayout->addWidget(new QLabel("反馈接收端口:"));
    feedbackPortLayout->addWidget(m_feedbackPortSpin);
    mainLayout->addLayout(feedbackPortLayout);

    m_addressEdit = new QTextEdit(this);
    m_addressEdit->setPlaceholderText("请输入地址（格式 ip:port），每行一个，例如：\n239.255.0.1:12345\n192.168.0.2:45678");
    m_addressEdit->setFixedHeight(80);
    mainLayout->addWidget(new QLabel("地址列表（广播时只用一行）:"));
    mainLayout->addWidget(m_addressEdit);

    // 数据包参数布局
    m_packetSizeSpin = new QSpinBox(this);
    m_packetSizeSpin->setRange(1, 65473);
    m_packetSizeSpin->setValue(1024);
    m_packetCountSpin = new QSpinBox(this);
    m_packetCountSpin->setRange(1, 1000000);
    m_packetCountSpin->setValue(10);
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(1, 10000);
    m_intervalSpin->setValue(10);

    QHBoxLayout *paramLayout = new QHBoxLayout;
    paramLayout->addWidget(new QLabel("数据包大小:"));
    paramLayout->addWidget(m_packetSizeSpin);
    paramLayout->addWidget(new QLabel("数据包数量:"));
    paramLayout->addWidget(m_packetCountSpin);
    mainLayout->addLayout(paramLayout);

    m_pacingModeCombo = new QComboBox(this);
    m_pacingModeCombo->addItem("目标速率", "rate");
    m_pacingModeCombo->addItem("固定间隔", "interval");
    m_targetMbpsSpin = new QDoubleSpinBox(this);
    m_targetMbpsSpin->setRange(0.001, 10000.0);
    m_targetMbpsSpin->setDecimals(3);
    m_targetMbpsSpin->setValue(10.0);
    m_targetMbpsSpin->setSuffix(" Mbps");
    m_intervalSpin->setEnabled(false);
    connect(m_pacingModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        const bool fixed = m_pacingModeCombo->itemData(index).toString() == "interval";
        m_intervalSpin->setEnabled(fixed);
        m_targetMbpsSpin->setEnabled(!fixed);
    });
    QHBoxLayout *pacingLayout = new QHBoxLayout;
    pacingLayout->addWidget(new QLabel("发送控制:"));
    pacingLayout->addWidget(m_pacingModeCombo);
    pacingLayout->addWidget(m_targetMbpsSpin);
    pacingLayout->addWidget(new QLabel("间隔 (ms):"));
    pacingLayout->addWidget(m_intervalSpin);
    mainLayout->addLayout(pacingLayout);

    // 延迟模式布局



    // 控制按钮布局
    m_configureButton = new QPushButton("配置测试", this);
    m_startButton = new QPushButton("开始测试", this);
    m_stopButton = new QPushButton("停止测试", this);
    m_sendFileButton = new QPushButton("发送文件", this);
    m_statsButton = new QPushButton("时延统计分析", this);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_configureButton);
    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addWidget(m_sendFileButton);
    buttonLayout->addWidget(m_statsButton);
    mainLayout->addLayout(buttonLayout);

    // 状态显示区域
    mainLayout->addWidget(new QLabel("系统状态:"));
    m_statusLabel = new QLabel("未配置", this);
    m_normalTextColor = m_statusLabel->palette().color(QPalette::WindowText);
    mainLayout->addWidget(m_statusLabel);

    // 添加分隔线
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line);

    // 告警状态栏
    mainLayout->addWidget(new QLabel("告警信息:"));
    m_alarmStatusLabel = new QLabel("无告警", this);
    QPalette alarmPalette = m_alarmStatusLabel->palette();
    alarmPalette.setColor(QPalette::WindowText, Qt::red);
    m_alarmStatusLabel->setPalette(alarmPalette);
    mainLayout->addWidget(m_alarmStatusLabel);

    // 数据包日志
    m_packetLogTable = new QTableWidget(this);
    m_packetLogTable->setColumnCount(2);
    m_packetLogTable->setHorizontalHeaderLabels({"序列号", "时间戳 (ns)"});
    m_packetLogTable->setRowCount(0);
    mainLayout->addWidget(new QLabel("数据包日志:"));
    mainLayout->addWidget(m_packetLogTable);

    // 队列统计表
    m_queueStatsTable = new QTableWidget(this);
    m_queueStatsTable->setColumnCount(3);
    m_queueStatsTable->setHorizontalHeaderLabels({"优先级", "队列长度", "平均/最大时延 (ns)"});
    m_queueStatsTable->setRowCount(4);
    for (int i = 0; i < 4; ++i) {
        m_queueStatsTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        m_queueStatsTable->setItem(i, 1, new QTableWidgetItem("0"));
        m_queueStatsTable->setItem(i, 2, new QTableWidgetItem("0 / 0"));
    }
    mainLayout->addWidget(new QLabel("队列统计:"));
    mainLayout->addWidget(m_queueStatsTable);

    // 表格属性设置
    m_packetLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_queueStatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 信号连接
    connect(m_configureButton, &QPushButton::clicked, this, &TSMMainWindow::onConfigureClicked);
    connect(m_startButton, &QPushButton::clicked, this, &TSMMainWindow::onStartClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &TSMMainWindow::onStopClicked);
    connect(m_sendFileButton, &QPushButton::clicked, this, &TSMMainWindow::onSendFileClicked);
    connect(m_controller, &TSM_Controller::testStatusChanged, this, &TSMMainWindow::updateStatus);
    connect(m_controller, &TSM_Controller::packetSent, this, &TSMMainWindow::updatePacketLog);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, &TSMMainWindow::onTypeChanged);
    connect(m_schedulingAlgoCombo, &QComboBox::currentTextChanged, this, [=](const QString &text) {
        m_controller->setSchedulingAlgorithm(text == "先进先出队列" ? PacketScheduler::FIFO : PacketScheduler::WFQ);
    });
    connect(m_priorityDistCombo, &QComboBox::currentTextChanged, this, [=](const QString &text) {
        m_controller->setPriorityDistribution(text == "均匀分布" ? PacketScheduler::Uniform : PacketScheduler::Skewed);
    });
    connect(m_controller->scheduler(), &PacketScheduler::queueLengthChanged, this, &TSMMainWindow::updateQueueStats);

    // 关联异常时延阈值设置
    connect(m_delayThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            m_controller, &TSM_Controller::setDelayThreshold);

    // 关联阈值超限次数设置
    connect(m_abnormalCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            m_controller, &TSM_Controller::setAbnormalCountThreshold);

    // 告警显示处理
    connect(m_controller, &TSM_Controller::delayAlarmTriggered, this, [=](qint64 actual, int threshold, int count) {
        m_alarmStatusLabel->setText(QString("【严重告警】总时延%1 ms连续%2次超过阈值%3 ms！")
                                        .arg(actual).arg(count).arg(threshold));
    });

    // 统计分析相关连接
    connect(m_statsButton, &QPushButton::clicked, [=]() {
        if (m_statsDialog->isHidden()) {
            m_statsDialog->show();
        } else {
            m_statsDialog->raise(); // 若已显示则置顶
        }
    });

    // 使用lambda表达式确保参数正确转换和传递
    connect(m_controller, &TSM_Controller::delayStatsUpdatedTCP,
            m_statsDialog, [=](qint64 a, qint64 b, qint64 c) {
                qDebug() << "TCP统计数据接收 - A:" << a << " B:" << b << " C:" << c;
                m_statsDialog->updateStats(a, static_cast<float>(b), static_cast<float>(c));
            });

    connect(m_controller, &TSM_Controller::delayStatsUpdatedUDP,
            m_statsDialog, [=](qint64 a, float b, float c) {
                qDebug() << "UDP统计数据接收 - A:" << a << " B:" << b << " C:" << c;
                m_statsDialog->updateStats(a, b, c);
            });

    // 初始设置默认值
    m_controller->setDelayThreshold(m_delayThresholdSpin->value());
    m_controller->setAbnormalCountThreshold(m_abnormalCountSpin->value());
}

TSMMainWindow::~TSMMainWindow() {
    delete m_statsDialog;
}

void TSMMainWindow::onTypeChanged(const QString &type) {
    m_protocolCombo->setEnabled(type == "单播");
}

void TSMMainWindow::onConfigureClicked() {
    QVariantMap params;
    QString type = m_typeCombo->currentText();
    params["type"] = type;

    if (type == "单播") {
        params["protocol"] = m_protocolCombo->currentText().toLower();
    }

    QStringList lines = m_addressEdit->toPlainText().split("\n", Qt::SkipEmptyParts);

    if (type == "广播") {
        if (lines.isEmpty()) {
            QMessageBox::warning(this, "错误", "请输入广播地址:端口，例如 192.168.1.255:12345");
            return;
        }
        QString input = lines.first().trimmed();
        QStringList parts = input.split(":");
        if (parts.size() != 2) {
            QMessageBox::warning(this, "错误", "请输入格式正确的地址:端口，例如 192.168.1.255:12345");
            return;
        }

        QHostAddress addr(parts[0].trimmed());
        bool ok = false;
        quint16 port = parts[1].toUShort(&ok);

        if (addr.isNull() || !ok || port == 0) {
            QMessageBox::warning(this, "错误", "无效的地址或端口，请检查输入");
            return;
        }

        params["broadcastAddress"] = addr.toString();
        params["port"] = port;
    }
    else {
        QVariantList targetList;
        bool isMulticast = (type == "多播");
        QHostAddress multicastGroup;

        for (const QString &line : lines) {
            QStringList parts = line.trimmed().split(":");
            if (parts.size() != 2) continue;

            QHostAddress addr(parts[0].trimmed());
            bool ok = false;
            quint16 port = parts[1].toUShort(&ok);

            if (!addr.isNull() && ok) {
                if (isMulticast) {
                    if (!addr.isMulticast()) {
                        QMessageBox::warning(this, "错误",
                                             "组播模式需要组播地址(224.0.0.0-239.255.255.255)\n"
                                             "无效地址: " + addr.toString());
                        return;
                    }
                    if (multicastGroup.isNull()) {
                        multicastGroup = addr;
                    } else if (multicastGroup != addr) {
                        QMessageBox::warning(this, "错误",
                                             "组播模式下所有目标地址必须属于同一组播组");
                        return;
                    }
                }

                QVariantList pair;
                pair << addr.toString() << port;
                targetList << QVariant(pair);
            }
        }

        params["targets"] = targetList;

        if (isMulticast && !multicastGroup.isNull()) {
            params["multicastGroup"] = multicastGroup.toString();
            params["multicastPort"] = targetList.first().toList()[1].toUInt();
        }
    }

    params["packetSize"] = m_packetSizeSpin->value();
    params["packetCount"] = m_packetCountSpin->value();
    params["pacingMode"] = m_pacingModeCombo->currentData().toString();
    params["targetMbps"] = m_targetMbpsSpin->value();
    params["interval"] = m_intervalSpin->value();


    quint16 feedbackPort = m_feedbackPortSpin->value();
    m_controller->generator()->setFeedbackPort(feedbackPort);

    m_controller->configureTest(params);
    m_alarmStatusLabel->setText("无告警");
}

void TSMMainWindow::onStartClicked() {
    m_packetLogTable->setRowCount(0);
    QPalette normalPalette = m_statusLabel->palette();
    normalPalette.setColor(QPalette::WindowText, m_normalTextColor);
    m_statusLabel->setPalette(normalPalette);
    m_controller->startTest();
    m_alarmStatusLabel->setText("无告警");
}

void TSMMainWindow::onStopClicked() {
    m_controller->stopTest();
    m_alarmStatusLabel->setText("无告警");
}

void TSMMainWindow::onSendFileClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择文件", "", "所有文件 (*.*)");
    if (filePath.isEmpty()) {
        QMessageBox::information(this, "提示", "未选择文件");
        return;
    }

    QVariantMap params;
    params["filePath"] = filePath;
    params["packetSize"] = m_packetSizeSpin->value();
    params["pacingMode"] = m_pacingModeCombo->currentData().toString();
    params["targetMbps"] = m_targetMbpsSpin->value();
    params["interval"] = m_intervalSpin->value();
    m_packetLogTable->setRowCount(0);
    QPalette normalPalette = m_statusLabel->palette();
    normalPalette.setColor(QPalette::WindowText, m_normalTextColor);
    m_statusLabel->setPalette(normalPalette);
    m_controller->sendFile(params);
    m_alarmStatusLabel->setText("无告警");
}

void TSMMainWindow::updateStatus(const QString &status) {
    m_statusLabel->setText(status);
}

void TSMMainWindow::updatePacketLog(const QVector<QPair<qint64, qint64>> &packets) {
    m_packetLogTable->setRowCount(0);
    for (const auto &packet : packets) {
        int row = m_packetLogTable->rowCount();
        m_packetLogTable->insertRow(row);
        m_packetLogTable->setItem(row, 0, new QTableWidgetItem(QString::number(packet.first)));
        m_packetLogTable->setItem(row, 1, new QTableWidgetItem(QString::number(packet.second)));
    }
}

void TSMMainWindow::updateQueueStats(int priority, int length) {
    m_queueStatsTable->setItem(priority, 1, new QTableWidgetItem(QString::number(length)));
    QVector<qint64> avgDelays = m_controller->scheduler()->getAverageQueueDelays();
    QVector<qint64> maxDelays = m_controller->scheduler()->getMaxQueueDelays();
    m_queueStatsTable->setItem(priority, 2, new QTableWidgetItem(
                                                QString("%1 / %2").arg(avgDelays[priority]).arg(maxDelays[priority])));
}
