#include "delayinjector.h"
#include "qdebug.h"

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
    qint64 distanceMeters = static_cast<qint64>(m_distance) * 1000; // 距离，m

    // 5G
    double relativePermittivity = 1.5;
    double fiveGSpeed = speedOfLight / std::sqrt(relativePermittivity);

    // 随机抖动
    double jitter = 0.0001;

    // 光纤
    constexpr double fiberRefractiveIndex = 1.467;
    double fiberSpeed = speedOfLight / fiberRefractiveIndex;
    double dispersionDelay = distanceMeters * 1e-9; // 1ns/km

    // 卫星
    constexpr double orbitHeight = 35786e3; // GEO轨道高度，m
    constexpr double earthRadius = 6371e3;  // 地球半径，m
    const double elevationAngle = 45.0;     // 仰角45度
    double elevationRad = elevationAngle * M_PI / 180.0;
    double slantRange = std::sqrt(std::pow(earthRadius + orbitHeight, 2)
                                  - std::pow(earthRadius * std::cos(elevationRad), 2))
                        - earthRadius * std::sin(elevationRad);

    double roundTripPropagationDelay = 2 * slantRange / speedOfLight;

    double delaySeconds = 0.0;
    qint64 fixedDelayMs = 0;

    qDebug() << "DelayInjector mode:" << m_mode << ", slantRange:" << slantRange
             << ", roundTripPropagationDelay:" << roundTripPropagationDelay;

    switch (m_mode) {
    case None:
        delaySeconds = 0;
        break;
    case Satellite:
        delaySeconds = roundTripPropagationDelay;
        fixedDelayMs = 0;
        break;
    case Fiber:
        delaySeconds = (distanceMeters / fiberSpeed) + dispersionDelay + jitter;
        fixedDelayMs = 0;
        break;
    case FiveG:
        delaySeconds = (distanceMeters / fiveGSpeed) + jitter;
        fixedDelayMs = 0;
        break;
    }

    qint64 delayNs = static_cast<qint64>(delaySeconds * 1e6) * 1000 + fixedDelayMs * 1000000;
    qDebug() << "delaySeconds:" << delaySeconds << ", delayNs:" << delayNs;
    return delayNs;
}
