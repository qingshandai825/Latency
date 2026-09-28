#pragma once
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QThread>
#include <QMap>
#include <QTabWidget>
#include <QDialog>
#include <QVector>
#include <QLabel>
#include <QWidget>
#include <QGroupBox>
#include <QPainter> // 新增：包含 QPainter 头文件
#include <algorithm> // 用于 std::min_element 和 std::max_element
#include <cmath> // 用于 floor 和 ceil
#include "networkworker.h"
#include "sender_gui/background_sender.h"
#include "simpleplotwidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindowLi; }
QT_END_NAMESPACE

// 自定义直方图控件
class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(100);
        setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    }

    void setData(const QVector<double> &data) {
        m_data = data;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);

        if (m_data.isEmpty()) return;

        double minVal = *std::min_element(m_data.begin(), m_data.end());
        double maxVal = *std::max_element(m_data.begin(), m_data.end());
        if (minVal == maxVal) {
            painter.drawLine(10, height()/2, width()-10, height()/2);
            return;
        }

        const int bins = 10;
        int binCounts[bins] = {0};

        for (double val : m_data) {
            int bin = static_cast<int>((val - minVal) / (maxVal - minVal) * bins);
            if (bin >= bins) bin = bins - 1;
            if (bin < 0) bin = 0;
            binCounts[bin]++;
        }

        int maxCount = *std::max_element(binCounts, binCounts + bins);
        if (maxCount == 0) return;

        int margin = 20;
        QRect rect = this->rect().adjusted(margin, margin, -margin, -margin);
        int width = rect.width();
        int height = rect.height();

        painter.drawLine(rect.bottomLeft(), rect.bottomRight());
        painter.drawLine(rect.bottomLeft(), rect.topLeft());

        int binWidth = width / bins;
        for (int i = 0; i < bins; i++) {
            int barHeight = static_cast<int>((binCounts[i] * 1.0 / maxCount) * height);
            QRect barRect(rect.left() + i * binWidth, rect.bottom() - barHeight, binWidth - 2, barHeight);
            painter.fillRect(barRect, QColor(0, 120, 215));
            painter.drawRect(barRect);
        }

        painter.drawText(rect.bottomLeft().x() + width/2 - 40, rect.bottom() + margin,
                         QString("范围: %1 - %2").arg(minVal, 0, 'f', 1).arg(maxVal, 0, 'f', 1));
    }

private:
    QVector<double> m_data;

};

// 时延统计对话框
class DelayStatsDialogLi : public QDialog {
    Q_OBJECT
public:
    explicit DelayStatsDialogLi(QWidget *parent = nullptr);

    // 更新统计数据
    void updateStats(double a, double b, double c);
    void clearStats();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // 存储历史数据
    QVector<double> m_aDurations; // A端（rttMs）
    QVector<double> m_bDurations; // B端（forwarderElapsed）
    QVector<double> m_cDurations; // C端（receiverElapsed）

    // UI 组件
    QLabel *m_aPercentLabel; // A端占比
    QLabel *m_bPercentLabel; // B端占比
    QLabel *m_cPercentLabel; // C端占比

    QLabel *m_aP50Label; // A端 P50
    QLabel *m_aP90Label; // A端 P90
    QLabel *m_aP99Label; // A端 P99

    QLabel *m_bP50Label; // B端 P50
    QLabel *m_bP90Label; // B端 P90
    QLabel *m_bP99Label; // B端 P99

    QLabel *m_cP50Label; // C端 P50
    QLabel *m_cP90Label; // C端 P90
    QLabel *m_cP99Label; // C端 P99

    QLabel *m_aStdLabel; // A端标准差
    QLabel *m_bStdLabel; // B端标准差
    QLabel *m_cStdLabel; // C端标准差

    // 直方图绘制区域
    HistogramWidget *m_aHistogram; // 修改：明确声明为 HistogramWidget *
    HistogramWidget *m_bHistogram;
    HistogramWidget *m_cHistogram;

    // 计算百分位值
    double calculatePercentile(QVector<double> &sortedData, double percentile);
    // 计算标准差
    double calculateStdDev(QVector<double> &data);
    // 更新直方图
    void updateHistograms();
};

class MainWindowLi : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindowLi(QWidget *parent = nullptr);
    ~MainWindowLi();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onClearChartClicked();
    void onInstantProbeClicked();
    void onRTTMeasured(double rtt, int bgMbps, bool isRetransmit);
    void onStatusUpdate(const QString &msg);
    void onRTTAlarm(const QString &alarmMsg);
    void onRTTRecovered(const QString &recoverMsg);
    void onStatsButtonClicked();
    void startBgButton();
    void stopBgButton();
    void onDelayThresholdChanged(double threshold);

private:
    Ui::MainWindowLi *ui;
    NetworkWorker *worker;
    QThread *workerThread;
    QMap<int, SimplePlotWidget*> chartMap;
    QMap<int, int> seqMap;
    QTabWidget *chartTabs;
    int delayThreshold_ = 100;
    int abnormalCountThreshold_ = 3;
    DelayStatsDialogLi *m_statsDialog;

    BackgroundSender* bgWorker_;  // 独立后台流量对象
    QThread* bgThread_;  // 后台线程
};
