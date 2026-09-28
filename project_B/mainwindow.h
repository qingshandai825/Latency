#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTcpServer>
#include <PacketReceiver.h>
#include <QUdpSocket>
#include <QWidget>
#include "delayinjector.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QWidget {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_connectButton_clicked();
    void on_listenButton_clicked();
    void on_disconnectButton_clicked();
    void on_stopListenButton_clicked();
    void on_newConnection();
    void onPacketReceivedFromA(const QByteArray &packet, quint8 proto);

    void on_readyRead();
    void on_udpReadyRead();
    void on_connected();
    void on_disconnected();
    void on_errorOccurred(QAbstractSocket::SocketError socketError);

    void onLogMessageReceived(const QString &msg); // 日志显示槽
    void onTcpPacketsProcessed(const QList<QByteArray> &packets);

    // ✅ 新增槽函数：处理批次统计信息
    void onBatchProcessingTime(quint32 batchNumber, double timeMs);
    void onFeedbackReadyRead();
    void onUdpFeedbackReadyRead();
    // 新增槽函数，用于处理延迟模式和距离的变化
    void on_delaySettingsChanged();

private:
    Ui::MainWindow *ui;
    QTcpSocket *clientSocket;
    QTcpServer *server;
    QTcpSocket *serverClientSocket;
    PacketReceiver *receiver;
    QUdpSocket *udpSocket = nullptr;
    QUdpSocket *udpSendSocket = nullptr;
    QHostAddress forwardTargetAddress;
    quint16 forwardTargetPort = 0;

    QTimer *logFlushTimer = nullptr;  // 日志刷新定时器
    QStringList logBuffer;

    // TCP/UDP包计数
    int tcpPacketCount = 0;
    int udpPacketCount = 0;
    QMap<quint32, double> batchProcessTimeMap;
    int consecutiveUdpAbnormalCount = 0;  // 连续UDP异常计数
    int consecutiveTcpAbnormalCount = 0;  // 连续TCP异常计数
    bool isProcessingTcpData = false;
    // 新增 DelayInjector 成员
    DelayInjector *delayInjector;
};

#endif // MAINWINDOW_H
