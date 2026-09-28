#include "packet_scheduler.h"
#include <QDebug>
#include <QRandomGenerator>
#include <algorithm>

PacketScheduler::PacketScheduler(QObject *parent)
    : QObject(parent),
    m_schedulingAlgorithm(FIFO),
    m_priorityDistribution(Uniform),
    m_totalPacketsProcessed(0)
{
    // 初始化四个优先级队列
    m_queues.resize(4); // 每个优先级一个队列（0到3）
    m_queueStats.resize(4); // 每个优先级的统计信息
    for (int i = 0; i < 4; ++i) {
        m_queueStats[i] = {0, 0, 0}; // {计数，总时延，最大时延}
    }

    // 启动定时器，定期检查队列并处理数据包
    m_processTimer = new QTimer(this);
    m_processTimer->setTimerType(Qt::PreciseTimer);
    connect(m_processTimer, &QTimer::timeout, this, &PacketScheduler::processQueues);
    m_processTimer->start(1); // 每1毫秒检查队列
}

void PacketScheduler::setSchedulingAlgorithm(SchedulingAlgorithm algorithm) {
    m_schedulingAlgorithm = algorithm;
    qDebug() << "调度算法设置为：" << (algorithm == FIFO ? "FIFO" : "WFQ");
}

void PacketScheduler::setPriorityDistribution(PriorityDistribution distribution) {
    m_priorityDistribution = distribution;
    qDebug() << "优先级分布设置为：" << (distribution == Uniform ? "均匀分布" : "偏斜分布");
}

void PacketScheduler::schedulePacket(qint64 sendTime, const QByteArray &packet,
                                     const QHostAddress &dest, quint16 port, quint64 delayNs, quint8 priority) {
    if (priority > 3) {
        qWarning() << "无效的优先级：" << priority << "，设置为0";
        priority = 0;
    }

    qint64 now = m_clock.currentTime();
    PacketInfo packetInfo = {sendTime, packet, dest, port, delayNs, now, priority};

    // 将数据包加入对应优先级队列
    m_queues[priority].enqueue(packetInfo);
    m_queueStats[priority].count++;
    emit queueLengthChanged(priority, m_queues[priority].size());

    qDebug() << "数据包入队：优先级=" << priority << "，队列长度=" << m_queues[priority].size();
}

void PacketScheduler::processQueues() {
    if (m_schedulingAlgorithm == FIFO) {
        processFifo();
    } else {
        processWfq();
    }
}

void PacketScheduler::processFifo() {
    qint64 now = m_clock.currentTime();

    // 按优先级顺序（从高到低）处理队列
    for (int priority = 0; priority < 4; ++priority) {
        while (!m_queues[priority].isEmpty()) {
            PacketInfo packetInfo = m_queues[priority].head();
            if (packetInfo.sendTime <= now) {
                m_queues[priority].dequeue();
                emit sendImmediately(packetInfo.packet, packetInfo.dest, packetInfo.port, packetInfo.delayNs);

                // 计算并更新排队时延统计
                qint64 queuingDelay = now - packetInfo.enqueueTime;
                m_queueStats[priority].totalDelayNs += queuingDelay;
                m_queueStats[priority].maxDelayNs = qMax(m_queueStats[priority].maxDelayNs, queuingDelay);
                m_totalPacketsProcessed++;

                emit queueLengthChanged(priority, m_queues[priority].size());
                qDebug() << "处理数据包：优先级=" << priority << "，排队时延=" << queuingDelay << "纳秒";
            } else {
                break; // 如果下一个数据包未到发送时间，停止处理
            }
        }
    }
}

void PacketScheduler::processWfq() {
    qint64 now = m_clock.currentTime();
    // WFQ权重：高优先级分配更高权重
    static const int weights[] = {8, 4, 2, 1}; // 优先级0（最高）权重8，依次递减
    int totalWeight = 15; // 权重总和

    // 根据权重处理队列
    for (int i = 0; i < totalWeight; ++i) {
        for (int priority = 0; priority < 4; ++priority) {
            if (i < weights[priority] && !m_queues[priority].isEmpty()) {
                PacketInfo packetInfo = m_queues[priority].head();
                if (packetInfo.sendTime <= now) {
                    m_queues[priority].dequeue();
                    emit sendImmediately(packetInfo.packet, packetInfo.dest, packetInfo.port, packetInfo.delayNs);

                    // 计算并更新排队时延统计
                    qint64 queuingDelay = now - packetInfo.enqueueTime;
                    m_queueStats[priority].totalDelayNs += queuingDelay;
                    m_queueStats[priority].maxDelayNs = qMax(m_queueStats[priority].maxDelayNs, queuingDelay);
                    m_totalPacketsProcessed++;

                    emit queueLengthChanged(priority, m_queues[priority].size());
                    qDebug() << "处理数据包（WFQ）：优先级=" << priority << "，排队时延=" << queuingDelay << "纳秒";
                }
            }
        }
    }
}

QVector<int> PacketScheduler::getQueueLengths() const {
    QVector<int> lengths(4);
    for (int i = 0; i < 4; ++i) {
        lengths[i] = m_queues[i].size();
    }
    return lengths;
}

QVector<qint64> PacketScheduler::getAverageQueueDelays() const {
    QVector<qint64> delays(4);
    for (int i = 0; i < 4; ++i) {
        delays[i] = m_queueStats[i].count > 0
                        ? m_queueStats[i].totalDelayNs / m_queueStats[i].count
                        : 0;
    }
    return delays;
}

QVector<qint64> PacketScheduler::getMaxQueueDelays() const {
    QVector<qint64> delays(4);
    for (int i = 0; i < 4; ++i) {
        delays[i] = m_queueStats[i].maxDelayNs;
    }
    return delays;
}

quint8 PacketScheduler::generatePriority() const {
    if (m_priorityDistribution == Uniform) {
        return QRandomGenerator::global()->bounded(4); // 均匀分布：0到3
    } else {
        // 偏斜分布：低优先级概率更高
        static const int weights[] = {40, 30, 20, 10}; // 优先级0: 40%, 1: 30%, 2: 20%, 3: 10%
        int total = 100;
        int rand = QRandomGenerator::global()->bounded(total);
        int sum = 0;
        for (int i = 0; i < 4; ++i) {
            sum += weights[i];
            if (rand < sum) {
                return i;
            }
        }
        return 3; // 默认值
    }
}
