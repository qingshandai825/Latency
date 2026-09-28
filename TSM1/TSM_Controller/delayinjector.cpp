#include "delayinjector.h"

DelayInjector::DelayInjector(QObject *parent) : QObject(parent), m_mode(Satellite), m_distance(D100km) {
}

void DelayInjector::setMode(DelayMode mode) {
    m_mode = mode;
}

void DelayInjector::setDistance(Distance distance) {
    m_distance = distance;
}

qint64 DelayInjector::calculateDelay() const {
    double speedOfLight = 3.0e8; // 光速，m/s
    // double fiberSpeed = 2.0e8;   // 光纤传播速度，m/s
    qint64 distanceMeters = static_cast<qint64>(m_distance) * 1000; // 距离，m

    //5G
    double relativePermittivity = 1.5; // 典型大气条件
    double fiveGSpeed = speedOfLight / std::sqrt(relativePermittivity);

    // 5. 随机抖动（平均0ms，标准差100μs）
    double jitter = 0.0001;

    double delaySeconds = 0.0;
    qint64 fixedDelayMs = 0;


    constexpr double fiberRefractiveIndex = 1.467; // 光纤折射率
    double fiberSpeed = speedOfLight / fiberRefractiveIndex;
    // 3. 色散补偿时延
    double dispersionDelay = distanceMeters * 1e-9; // 1ns/km = 1ns


    //卫星
    constexpr double orbitHeight = 35786e3; // GEO轨道高度，m
    constexpr double earthRadius = 6371e3;  // 地球半径，m
    const double elevationAngle = 45.0;     // 典型仰角45度

    double elevationRad = elevationAngle * M_PI / 180.0;
    double slantRange = std::sqrt(std::pow(earthRadius + orbitHeight, 2)
                                  - std::pow(earthRadius * std::cos(elevationRad), 2))
                        - earthRadius * std::sin(elevationRad);

    double roundTripPropagationDelay = 2 * slantRange / speedOfLight;

    switch (m_mode) {
    case None:
        delaySeconds =  0;
        break;
    case Satellite:
        // 卫星通信固定时延（同步卫星往返约 72,000km）
        delaySeconds = roundTripPropagationDelay; // 240ms
        fixedDelayMs = 0; // 额外处理延迟
        break;
    case Fiber:
        delaySeconds = (distanceMeters / fiberSpeed)+dispersionDelay+jitter;
        fixedDelayMs = 0; // 光纤设备处理延迟
        break;
    case FiveG:
        delaySeconds = (distanceMeters / fiveGSpeed)+jitter;
        fixedDelayMs = 0; // 5 Bodega5G网络处理延迟
        break;
    }

    // 转换为纳秒
    return static_cast<qint64>(delaySeconds * 1e9) + fixedDelayMs * 1000000;
}
