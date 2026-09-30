#include "packet_scheduler.h"
#include <QRandomGenerator>

PacketScheduler::PacketScheduler(QObject *parent)
    : QObject(parent), m_schedulingAlgorithm(FIFO),
      m_priorityDistribution(Uniform), m_totalPacketsProcessed(0)
{
    m_queues.resize(4);
    m_queueStats.resize(4);
    for (int i = 0; i < 4; ++i) m_queueStats[i] = {0, 0, 0};
    m_pacingClock.start();
    m_processTimer = new QTimer(this);
    m_processTimer->setTimerType(Qt::PreciseTimer);
    connect(m_processTimer, &QTimer::timeout, this, &PacketScheduler::processQueues);
    m_processTimer->start(1);
}

void PacketScheduler::setSchedulingAlgorithm(SchedulingAlgorithm algorithm) { m_schedulingAlgorithm = algorithm; }
void PacketScheduler::setPriorityDistribution(PriorityDistribution distribution) { m_priorityDistribution = distribution; }

void PacketScheduler::setRateMbps(double mbps) {
    m_rateMbps = qMax(0.001, mbps);
    m_intervalMs = 0;
    m_nextSendNs = m_pacingClock.nsecsElapsed();
}

void PacketScheduler::setFixedIntervalMs(int intervalMs) {
    m_intervalMs = qMax(1, intervalMs);
    m_nextSendNs = m_pacingClock.nsecsElapsed();
}

void PacketScheduler::setDispatcher(const std::function<bool(const QByteArray &, const QHostAddress &, quint16, quint64)> &dispatcher) {
    m_dispatcher = dispatcher;
}

void PacketScheduler::schedulePacket(const QByteArray &packet, const QHostAddress &dest,
                                     quint16 port, quint64 delayNs, quint8 priority) {
    if (priority > 3) priority = 0;
    m_queues[priority].enqueue({packet, dest, port, delayNs, m_clock.currentTime(), priority});
    ++m_queuedPackets;
    emit queueLengthChanged(priority, m_queues[priority].size());
}

int PacketScheduler::queuedPackets() const { return m_queuedPackets; }

void PacketScheduler::clear() {
    for (int priority = 0; priority < 4; ++priority) {
        m_queues[priority].clear();
        emit queueLengthChanged(priority, 0);
    }
    m_queuedPackets = 0;
    m_nextSendNs = m_pacingClock.nsecsElapsed();
}

int PacketScheduler::nextPriority() const {
    if (m_schedulingAlgorithm == FIFO) {
        for (int i = 0; i < 4; ++i) if (!m_queues[i].isEmpty()) return i;
        return -1;
    }
    static const int order[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,3};
    for (int i = 0; i < 15; ++i) {
        const int p = order[(m_wfqCursor + i) % 15];
        if (!m_queues[p].isEmpty()) return p;
    }
    return -1;
}

void PacketScheduler::processQueues() {
    if (!m_dispatcher) return;
    // Timer wakeups are approximate; the monotonic byte clock controls pacing.
    for (int i = 0; i < 64 && m_queuedPackets > 0; ++i) {
        const qint64 now = m_pacingClock.nsecsElapsed();
        if (now < m_nextSendNs) break;
        const int priority = nextPriority();
        if (priority < 0) break;
        const PacketInfo &head = m_queues[priority].head();
        if (!m_dispatcher(head.packet, head.dest, head.port, head.delayNs)) break;
        const qint64 bytes = head.packet.size() + (m_tcpMode ? 4 : 0);
        const qint64 queuedNs = m_clock.currentTime() - head.enqueueTime;
        m_queues[priority].dequeue();
        --m_queuedPackets;
        ++m_queueStats[priority].count;
        m_queueStats[priority].totalDelayNs += queuedNs;
        m_queueStats[priority].maxDelayNs = qMax(m_queueStats[priority].maxDelayNs, queuedNs);
        ++m_totalPacketsProcessed;
        if (m_schedulingAlgorithm == WFQ) m_wfqCursor = (m_wfqCursor + 1) % 15;
        const qint64 spacingNs = m_intervalMs > 0
            ? static_cast<qint64>(m_intervalMs) * 1000000LL
            : qMax<qint64>(1, static_cast<qint64>(bytes * 8000.0 / m_rateMbps));
        m_nextSendNs = qMax(now, m_nextSendNs) + spacingNs;
        emit queueLengthChanged(priority, m_queues[priority].size());
        emit packetDispatched();
    }
}

QVector<int> PacketScheduler::getQueueLengths() const {
    QVector<int> lengths(4);
    for (int i = 0; i < 4; ++i) lengths[i] = m_queues[i].size();
    return lengths;
}

QVector<qint64> PacketScheduler::getAverageQueueDelays() const {
    QVector<qint64> delays(4);
    for (int i = 0; i < 4; ++i)
        delays[i] = m_queueStats[i].count ? m_queueStats[i].totalDelayNs / m_queueStats[i].count : 0;
    return delays;
}

QVector<qint64> PacketScheduler::getMaxQueueDelays() const {
    QVector<qint64> delays(4);
    for (int i = 0; i < 4; ++i) delays[i] = m_queueStats[i].maxDelayNs;
    return delays;
}

quint8 PacketScheduler::generatePriority() const {
    if (m_priorityDistribution == Uniform) return QRandomGenerator::global()->bounded(4);
    const int r = QRandomGenerator::global()->bounded(100);
    return r < 40 ? 0 : r < 70 ? 1 : r < 90 ? 2 : 3;
}
