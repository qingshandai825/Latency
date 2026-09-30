#ifndef RECEIVER_WINDOW_H
#define RECEIVER_WINDOW_H

#include <QMainWindow>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QFile>
#include <QTemporaryFile>
#include <QSharedPointer>
#include <QQueue>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QComboBox>
#include <QTableWidget>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QPalette>  // 用于颜色控制
#include "high_res_clock.h"

class ReceiverWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ReceiverWindow(QWidget *parent = nullptr);

    QVector<int> getQueueLengths() const;
    QVector<qint64> getAverageQueueDelays() const;
    QVector<qint64> getMaxQueueDelays() const;
    void appendThroughputPoint(double throughputMbps);
    void updateThroughputChart();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onSaveFileClicked();
    void onNewConnection();
    void onProtocolChanged(const QString &protocol);
    void processPendingDatagrams();
    void processTcpData();
    void processPriorityQueues();
    void updateLatencyChart();
    // 阈值设置变更响应
    void onDelayThresholdChanged(int threshold);
    void onAbnormalCountChanged(int count);

private:
    struct PacketInfo;
    void setupUI();
    bool isValidLocalAddress(const QHostAddress &addr);
    bool configureSocket(const QHostAddress &listenAddr, quint16 port, const QString &protocol);
    void resetFileReceiving();
    void handleFilePacket(const PacketInfo &info);
    void failFileTransfer(quint32 batchNumber, const QString &reason);
    void updatePacketLossRate();
    void addPacketToTable(quint64 counter, const QHostAddress &sender, quint16 port,
                          quint64 sequence, qint64 timestamp, double latencyMs,
                          bool isFilePacket, quint8 priority = 0);
    int currentTestMbps() const;
    void appendLatencyPoint(double latencyMs);
    // 告警判断方法
    void checkDurationAbnormal(double durationMs, quint32 batchNumber);
    // 重置告警状态
    void resetAlertStatus();


    QUdpSocket *m_receiverSocket;
    QTcpServer *m_tcpServer;
    QList<QTcpSocket*> m_tcpClients;
    QLineEdit *m_listenAddressEdit;
    QSpinBox *m_portSpin;
    QComboBox *m_protocolCombo;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QPushButton *m_saveFileButton;
    QLabel *m_statusLabel;
    QLabel *m_packetLossLabel;
    QLabel *m_alertStatusLabel;  // 告警状态显示
    QTableWidget *m_packetTable;
    QChart *m_latencyChart;
    QChartView *m_chartView;
    QLineSeries *m_realtimeSeries;
    QValueAxis *m_axisX;
    QValueAxis *m_axisY;
    quint64 m_packetCounter;
    quint64 m_expectedSequence;
    quint64 m_firstSequence;
    bool m_isReceivingFile;
    struct CompletedFile {
        QString fileName;
        QSharedPointer<QTemporaryFile> tempFile;
    };
    QQueue<CompletedFile> m_completedFiles;
    quint64 m_latencyPointIndex;
    HighResClock m_clock;
    QComboBox *m_bandwidthSelector;


    // 优先级队列相关
    struct PacketInfo {
        quint64 counter;
        QHostAddress sender;
        quint16 senderPort;
        quint64 sequence;
        qint64 timestamp;
        double latencyMs;
        bool isFilePacket;
        bool isFileHeader;
        QByteArray fileData;
        quint8 priority;
        qint64 enqueueTime;       // 纳秒级入队时间
        quint32 batchNumber;      // 批次号
        quint32 totalPacketCount;
        QTcpSocket* clientSocket = nullptr;           // TCP 使用
        QHostAddress senderAddress;                   // UDP 使用
        quint32 packetSize; // 新增：记录整个数据包大小
    };
    struct FileTransfer {
        bool headerReceived = false;
        quint64 headerSequence = 0;
        quint32 totalPackets = 0;
        quint32 writtenPackets = 0;
        quint64 fileSize = 0;
        quint64 writtenBytes = 0;
        quint64 bufferedBytes = 0;
        QString fileName;
        QMap<quint64, QByteArray> pendingChunks;
        QSharedPointer<QTemporaryFile> tempFile;
    };
    QHash<quint32, FileTransfer> m_fileTransfers;
    QHash<quint32, QPair<quint64, quint64>> m_completedRanges;
    QQueue<PacketInfo> m_priorityQueues[4];
    int m_queueLengths[4];
    double m_totalQueueDelay[4];
    double m_maxQueueDelay[4];
    quint64 m_packetCountPerQueue[4];
    QLabel *m_queueLengthLabels[4];
    QLabel *m_avgQueueDelayLabels[4];
    QLabel *m_maxQueueDelayLabels[4];
    QTimer *m_queueProcessTimer;
    QHash<QTcpSocket*, QByteArray> m_clientBuffers;  // 每个客户端的缓冲区
    QSet<quint64> m_receivedSequences;

    // 批次统计结构体
    struct BatchInfo {
        quint32 batchNumber = 0;        // 批次号
        qint64 startTime = 0;           // 批次首包入队时间（纳秒）
        qint64 endTime = 0;             // 批次最后包处理完成时间（纳秒）
        quint32 totalPacketCount = 0;   // 该批次总包数
        quint32 receivedPacketCount = 0;   // 当前接收包数
        quint32 processedPacketCount = 0;  // 当前处理包数
        qint64 processingNs = 0; // 本地逐包处理时间，不含限速造成的到包间隔
    };

    QMap<quint32, BatchInfo> m_batchMap;  // key: 批次号，value: 该批次统计数据
    void sendFeedbackToSender(const QHostAddress &addr, quint16 port, QTcpSocket *tcpSocket, quint32 batchNumber, double durationMs);

    // 时延阈值告警相关
    QSpinBox* m_delayThresholdSpin;       // 处理时延阈值设置（ms）
    QSpinBox* m_abnormalCountSpin;        // 连续超限次数阈值设置
    int m_delayThreshold;                 // 当前阈值（ms）
    int m_abnormalCountThreshold;         // 连续超限次数阈值
    int m_abnormalDelayCount;             // 当前连续超限计数
    QColor m_normalStatusColor;           // 正常状态文本颜色


    //开始：吞吐量图相关变量
    QLineSeries *m_throughputSeries;
    QChart *m_throughputChart;
    QValueAxis *m_throughputAxisX;
    QValueAxis *m_throughputAxisY;
    QChartView *m_throughputChartView;
    int m_throughputPointIndex;
    int m_currentGroupPacketCount;
    quint64 m_currentGroupTotalBytes;
    qint64 m_currentGroupStartTime;
};

#endif // RECEIVER_WINDOW_H
