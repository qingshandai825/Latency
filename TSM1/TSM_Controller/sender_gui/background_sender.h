#pragma once
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QDateTime>

class BackgroundSender : public QObject
{
    Q_OBJECT
public:
    explicit BackgroundSender(QObject *parent = nullptr);
    ~BackgroundSender() override = default;

public slots:
    void start(const QString &ip, quint16 port, int pktSize, int mbps);
    void stop();

private slots:
    void onTimeout();

private:
    QTimer *timer_;
    QUdpSocket *socket_;
    QString ip_;
    quint16 port_;
    int pktSize_;
    int packetsPerInterval_;
    int sentInInterval_;
    qint64 totalBytes_;
    qint64 lastSec_;
    bool stopped_;
};
