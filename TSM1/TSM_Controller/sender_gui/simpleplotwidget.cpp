#include "simpleplotwidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

SimplePlotWidget::SimplePlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(700, 500);
}

void SimplePlotWidget::addPoint(double x, double y, bool isRetransmit)
{
    if (isRetransmit)
        retransData_.append(qMakePair(x, y));
    else
        successData_.append(qMakePair(x, y));
    update();
}

void SimplePlotWidget::clearPoints()
{
    successData_.clear();
    retransData_.clear();
    update();
}

void SimplePlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);

    // 合并所有点
    QVector<QPair<double,double>> all = successData_ + retransData_;
    if (all.isEmpty()) return;

    // 计算 x,y 范围
    double minX = all.first().first, maxX = minX;
    double minY = all.first().second, maxY = minY;
    for (auto &pt : all) {
        minX = std::min(minX, pt.first);
        maxX = std::max(maxX, pt.first);
        minY = std::min(minY, pt.second);
        maxY = std::max(maxY, pt.second);
    }
    // 防止除零
    if (minX == maxX) maxX += 1;
    if (minY == maxY) maxY += 1;

    // 绘图区留边距
    int margin = 120;
    QRect plotRect = rect().adjusted(margin, margin, -margin, -margin);

    // 画坐标轴
    p.setPen(Qt::black);
    p.drawLine(plotRect.bottomLeft(), plotRect.bottomRight());
    p.drawLine(plotRect.topLeft(),   plotRect.bottomLeft());

    // 字体设置
    QFont font = p.font(); font.setPointSize(11);
    p.setFont(font);

    // 横轴刻度：从 minX 到 maxX，以整数增量打刻度
    int x0 = static_cast<int>(std::floor(minX));
    int x1 = static_cast<int>(std::ceil(maxX));
    for (int xi = x0; xi <= x1; ++xi) {
        double tx = xi;
        int px = plotRect.left() + (tx - minX) / (maxX - minX) * plotRect.width();
        p.drawLine(px, plotRect.bottom(), px, plotRect.bottom() + 6);
        p.drawText(px - 10, plotRect.bottom() + 12, 20, 18,
                   Qt::AlignHCenter|Qt::AlignTop,
                   QString::number(xi));
    }

    // 纵轴刻度：6 段
    int yTicks = 6;
    for (int i = 0; i <= yTicks; ++i) {
        double ty = minY + (maxY - minY) * i / yTicks;
        int py = plotRect.bottom()
                 - (ty - minY) / (maxY - minY) * plotRect.height();
        p.drawLine(plotRect.left() - 6, py, plotRect.left(), py);
        double ms = ty / 100.0;  // 换算成 ms
        p.drawText(plotRect.left() - 60, py - 8, 50, 16,
                   Qt::AlignRight|Qt::AlignVCenter,
                   QString::number(ms, 'f', 1));
    }

    // 单位文字
    QFont ufont = font; ufont.setPointSize(13); ufont.setBold(true);
    p.setFont(ufont);
    // 纵轴单位
    int yUnitX = plotRect.left() - 80;
    int yUnitY = plotRect.top() + plotRect.height()/2 + 10;
    p.save(); p.translate(yUnitX, yUnitY); p.rotate(-90);
    p.drawText(0,0,"ms"); p.restore();
    // 横轴单位
    int xUnitX = plotRect.left() + plotRect.width()/2 - 30;
    int xUnitY = plotRect.bottom() + 44;
    p.drawText(xUnitX, xUnitY, "s#");

    // 恢复字体画数据
    p.setFont(font);

    // 绘制有效 RTT（蓝色实线+圆点）
    p.setPen(QPen(Qt::blue,2));
    QPoint last;
    for (int i = 0; i < successData_.size(); ++i) {
        auto &pt = successData_[i];
        int px = plotRect.left() + (pt.first - minX)/(maxX-minX) * plotRect.width();
        int py = plotRect.bottom() - (pt.second - minY)/(maxY-minY) * plotRect.height();
        if (i > 0) p.drawLine(last, QPoint(px,py));
        last = QPoint(px,py);
        p.setBrush(Qt::white);
        p.drawEllipse(last,4,4);
    }

    // 绘制重传 RTT（红色虚线方块）
    QPen redPen(Qt::red,2,Qt::DashLine);
    p.setPen(redPen);
    for (auto &pt : retransData_) {
        int px = plotRect.left() + (pt.first - minX)/(maxX-minX) * plotRect.width();
        int py = plotRect.bottom() - (pt.second - minY)/(maxY-minY) * plotRect.height();
        p.drawRect(px-3, py-3, 6, 6);
    }

    // 绘制图例
    int lx = plotRect.right() - 100, ly = plotRect.top() + 10;
    p.setPen(QPen(Qt::blue,2));
    p.drawLine(lx, ly, lx+20, ly); p.drawText(lx+25, ly+5, "有效RTT");
    ly += 20; p.setPen(redPen);
    p.drawLine(lx, ly, lx+20, ly); p.drawText(lx+25, ly+5, "重传RTT");
}
