#ifndef TSM_CONTROLLER_H
#define TSM_CONTROLLER_H
#include <QObject>
#include <QHostAddress>
#include <QVector>
#include <QPair>
#include <QVariantMap>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include "traffic_generator.h"
#include "packet_scheduler.h"
#include "delayinjector.h"
#include "high_res_clock.h"

class TSM_Controller : public QObject {
    Q_OBJECT
public:
    explicit TSM_Controller(QObject *parent = nullptr);
    void configureTest(const QVariantMap &params);
    void startTest();
    void stopTest();
    void sendFile(const QVariantMap &params);
    void setSchedulingAlgorithm(PacketScheduler::SchedulingAlgorithm algorithm);
    void setPriorityDistribution(PacketScheduler::PriorityDistribution distribution);
    TrafficGenerator* generator() { return m_generator; }
    PacketScheduler* scheduler() { return m_scheduler; }
    void setDelayThreshold(int threshold) { m_delayThreshold = threshold; }
    void setAbnormalCountThreshold(int count) { m_abnormalCountThreshold = count; }

signals:
    void testStatusChanged(const QString &status);
    void packetSent(const QVector<QPair<qint64, qint64>> &packets);
    void delayAlarmTriggered(qint64 actualDelay, int threshold, int abnormalCount);
    void feedbackReceivedTCP(qint64 aDuration, float bDurationMs, float cDurationMs, quint32 batchNumber);
    void feedbackReceivedUDP(qint64 aDuration, float bDurationMs, float cDurationMs, quint32 batchNumber);
    // 新增：发送时延统计数据信号
    void delayStatsUpdatedTCP(qint64 aDuration, float bDurationMs, float cDurationMs);
    void delayStatsUpdatedUDP(qint64 aDuration, float bDurationMs, float cDurationMs);

private slots:
    void pumpPackets();
    void onPacketDispatched();
    void onFeedbackReceivedTCP(const QByteArray &feedbackData);
    void onFeedbackReceivedUDP(const QByteArray &feedbackData);

private:
    void checkDelayAbnormal(qint64 totalDuration);
    void beginPacing(const QVariantMap &params);
    TrafficGenerator *m_generator;
    PacketScheduler *m_scheduler;
    DelayInjector *m_delayInjector;
    QVariantMap m_currentConfig;
    QList<QPair<QHostAddress, quint16>> m_targets;
    QElapsedTimer m_jobClock;
    quint64 m_sentPacketCount = 0;
    quint64 m_sentBytes = 0;
    qint64 m_activeProcessingNs = 0;
    quint64 m_expectedPacketCount = 0;
    double m_totalDuration = 0;
    double totalDuration = 0;
    qint64 totalDurationWithDecimals = 0;
    double m_testTotalMs = 0;
    double m_fileTotalDuration = 0;
    bool m_isTcpMode = false;
    quint32 m_batchTest = 0;
    quint32 m_batchFile = 0;
    quint32 m_nextBatch = 0;
    bool m_active = false;
    bool m_fileMode = false;
    bool m_productionDone = false;
    bool m_sendFailure = false;
    QFile m_file;
    QString m_fileName;
    quint64 m_fileSize = 0;
    quint32 m_jobPacketSize = 0;
    quint32 m_jobTotalPackets = 0;
    quint32 m_nextPacket = 0;
    quint32 m_jobBatch = 0;
    quint64 m_jobDelayNs = 0;
    QTimer *m_producerTimer = nullptr;
    int m_delayThreshold = 100;
    int m_abnormalCountThreshold = 3;
    int m_abnormalDelayCount = 0;
    HighResClock m_clock;
};

#endif
