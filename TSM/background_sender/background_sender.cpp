#include <QCoreApplication>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDebug>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 5) {
        qDebug() << "用法: <ip> <port> <pktSize> <MBps>";
        return -1;
    }

    const QString ip   = argv[1];
    //const quint16 port = QString(argv[2]).toUShort();
    const quint16 port = 6012;
    const int pktSize  = QString(argv[3]).toInt();      // ✅ 不再写死
    const double MBps  = QString(argv[4]).toDouble();   // MB/s(字节)

    if (port==0 || pktSize<=0 || MBps<=0) {
        qDebug() << "参数错误";
        return -1;
    }

    QUdpSocket sock;
    QByteArray payload(pktSize, 'X');

    // 速率参数
    const double bytesPerSec = MBps * 1024.0 * 1024.0;   // MB/s -> B/s
    double bucket = 0.0;                                 // 令牌桶（字节）
    const double bucketCap = bytesPerSec;                // 上限=1秒的量

    // 时钟与统计
    QElapsedTimer stepClock; stepClock.start();
    qint64 lastPrint = QDateTime::currentMSecsSinceEpoch();
    qint64 totalBytes = 0;

    // 高精度定时器，1ms 驱动
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(1);
    QObject::connect(&timer, &QTimer::timeout, [&](){
        // 补充令牌：按真实经过时间
        const double dt = stepClock.nsecsElapsed() / 1e9; // 秒
        stepClock.restart();
        bucket += dt * bytesPerSec;
        if (bucket > bucketCap) bucket = bucketCap;

        // 能发则发，不阻塞，不 usleep
        while (bucket >= pktSize) {
            qint64 n = sock.writeDatagram(payload, QHostAddress(ip), port);
            if (n < 0) break;                 // 内核缓冲满，等下个tick
            bucket -= pktSize;
            totalBytes += n;
        }

        // 每秒打印一次实际速率
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastPrint >= 1000) {
            double MBpsReal = totalBytes / (1024.0*1024.0);
            qDebug() << "📈 实际发送速率:" << QString::number(MBpsReal,'f',2) << "MB/s";
            totalBytes = 0;
            lastPrint = now;
        }
    });
    timer.start();

    return app.exec();
}
