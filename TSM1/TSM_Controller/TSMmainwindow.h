#ifndef TSMMAINWINDOW_H
#define TSMMAINWINDOW_H
#include <QWidget>
#include <QComboBox>
#include <QTextEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include "tsm_controller.h"
#include <QColor>
#include <QDialog>
#include <QVector>
#include <QPainter>
#include <QGroupBox>

// 时延统计对话框
class DelayStatsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DelayStatsDialog(QWidget *parent = nullptr);

    // 更新统计数据
    void updateStats(qint64 a, float b, float c);
    void clearStats();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // 存储历史数据
    QVector<qint64> m_aDurations;
    QVector<float> m_bDurations;
    QVector<float> m_cDurations;

    // UI组件
    QLabel *m_aPercentLabel;  // A端占比
    QLabel *m_bPercentLabel;  // B端占比
    QLabel *m_cPercentLabel;  // C端占比

    QLabel *m_aP50Label;      // A端P50
    QLabel *m_aP90Label;      // A端P90
    QLabel *m_aP99Label;      // A端P99

    QLabel *m_bP50Label;      // B端P50
    QLabel *m_bP90Label;      // B端P90
    QLabel *m_bP99Label;      // B端P99

    QLabel *m_cP50Label;      // C端P50
    QLabel *m_cP90Label;      // C端P90
    QLabel *m_cP99Label;      // C端P99

    QLabel *m_aStdLabel;      // A端标准差
    QLabel *m_bStdLabel;      // B端标准差
    QLabel *m_cStdLabel;      // C端标准差

    // 直方图绘制区域
    QWidget *m_aHistogram;
    QWidget *m_bHistogram;
    QWidget *m_cHistogram;

    // 计算百分位值
    double calculatePercentile(QVector<double> &sortedData, double percentile);
    // 计算标准差
    double calculateStdDev(QVector<double> &data);
    // 绘制直方图
    void drawHistogram(QWidget *widget, QVector<double> &data);
    // 新增：更新直方图的方法
    void updateHistograms();
};

class TSMMainWindow : public QWidget {
    Q_OBJECT
public:
    explicit TSMMainWindow(QWidget *parent = nullptr);
    ~TSMMainWindow() override;
signals:
    void delayAlarmTriggered(qint64 actualDelay, int threshold, int abnormalCount);
private slots:
    void onTypeChanged(const QString &type);
    void onConfigureClicked();
    void onStartClicked();
    void onStopClicked();
    void onSendFileClicked();
    void updateStatus(const QString &status);
    void updatePacketLog(const QVector<QPair<qint64, qint64>> &packets);
    void updateQueueStats(int priority, int length);

private:
    TSM_Controller *m_controller;
    QComboBox *m_typeCombo;
    QComboBox *m_protocolCombo;
    QTextEdit *m_addressEdit;
    QSpinBox *m_packetSizeSpin;
    QSpinBox *m_packetCountSpin;
    QSpinBox *m_intervalSpin;
    QComboBox *m_delayModeCombo;
    QComboBox *m_distanceCombo;
    QPushButton *m_configureButton;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QPushButton *m_sendFileButton;
    QLabel *m_statusLabel;
    QTableWidget *m_packetLogTable;
    QComboBox *m_schedulingAlgoCombo;
    QComboBox *m_priorityDistCombo;
    QTableWidget *m_queueStatsTable;
    QSpinBox *m_feedbackPortSpin;
    QDoubleSpinBox *m_delayThresholdSpin;
    QSpinBox *m_abnormalCountSpin;
    QLabel *m_alarmStatusLabel;
    QColor m_normalTextColor;

    // 统计分析相关
    QPushButton *m_statsButton;
    DelayStatsDialog *m_statsDialog;  // 对话框实例，只创建一次
};

#endif // TSMMAINWINDOW_H
