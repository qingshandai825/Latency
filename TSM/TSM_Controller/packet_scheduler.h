#ifndef PACKET_SCHEDULER_H
#define PACKET_SCHEDULER_H
#include <QObject>
#include <QTimer>
#include <QQueue>
#include <QVector>
#include <QHostAddress>
#include "high_res_clock.h"

class PacketScheduler : public QObject {
    Q_OBJECT
public:
    // 调度算法枚举
    enum SchedulingAlgorithm { FIFO, WFQ };
    // 优先级分布枚举
    enum PriorityDistribution { Uniform, Skewed };

    explicit PacketScheduler(QObject *parent = nullptr);

    // 设置调度算法
    void setSchedulingAlgorithm(SchedulingAlgorithm algorithm);
    // 设置优先级分布
    void setPriorityDistribution(PriorityDistribution distribution);
    // 调度数据包，新增优先级参数
    void schedulePacket(qint64 sendTime, const QByteArray &packet,
                        const QHostAddress &dest, quint16 port, quint64 delayNs, quint8 priority);
    // 获取各优先级队列长度
    QVector<int> getQueueLengths() const;
    // 获取各优先级平均排队时延
    QVector<qint64> getAverageQueueDelays() const;
    // 获取各优先级最大排队时延
    QVector<qint64> getMaxQueueDelays() const;
    // 生成优先级
    quint8 generatePriority() const;

signals:
    // 立即发送信号
    void sendImmediately(const QByteArray &packet, const QHostAddress &dest, quint16 port, quint64 delayNs);
    // 队列长度变化信号
    void queueLengthChanged(int priority, int length);

private:
    // 数据包信息结构体
    struct PacketInfo {
        qint64 sendTime;        // 计划发送时间
        QByteArray packet;      // 数据包内容
        QHostAddress dest;      // 目标地址
        quint16 port;           // 目标端口
        quint64 delayNs;        // 额外时延
        qint64 enqueueTime;     // 入队时间
        quint8 priority;        // 优先级（0-3）
    };

    // 队列统计结构体
    struct QueueStats {
        qint64 count;           // 处理的数据包数量
        qint64 totalDelayNs;    // 总排队时延（纳秒）
        qint64 maxDelayNs;      // 最大排队时延（纳秒）
    };

    // 私有方法
    void processQueues();        // 处理队列
    void processFifo();         // FIFO 调度
    void processWfq();          // WFQ 调度

    HighResClock m_clock;       // 高精度时钟
    QTimer *m_processTimer;     // 定时器用于定期检查队列
    QVector<QQueue<PacketInfo>> m_queues; // 四个优先级队列
    QVector<QueueStats> m_queueStats;     // 每个优先级的统计信息
    SchedulingAlgorithm m_schedulingAlgorithm; // 当前调度算法
    PriorityDistribution m_priorityDistribution; // 当前优先级分布
    qint64 m_totalPacketsProcessed; // 总处理数据包数
};

#endif
