#ifndef DELAY_INJECTOR_H
#define DELAY_INJECTOR_H

#include <QObject>

class DelayInjector : public QObject {
    Q_OBJECT
public:
    enum DelayMode {None, Satellite, Fiber, FiveG };
    enum Distance {D0km=0, D100km = 100, D1000km = 1000, D10000km = 10000,D35786km= 35786};

    explicit DelayInjector(QObject *parent = nullptr);

    // 设置模式和距离
    void setMode(DelayMode mode);
    void setDistance(Distance distance);

    // 计算时延（单位：纳秒）
    qint64 calculateDelay() const;

private:
    DelayMode m_mode;
    Distance m_distance;
};

#endif // DELAY_INJECTOR_H
