#pragma once

#include <QWidget>
#include <QVector>
#include <QPair>
#include <QPaintEvent>  // 需要此头来声明 QPaintEvent

class SimplePlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SimplePlotWidget(QWidget *parent = nullptr);

    // x: Probe 序号，y: 0.01ms 单位的 RTT
    void addPoint(double x, double y, bool isRetransmit);
    void clearPoints();

protected:
    // 重载 paintEvent
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPair<double,double>> successData_;
    QVector<QPair<double,double>> retransData_;
};
