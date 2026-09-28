#ifndef HIGH_RES_CLOCK_H
#define HIGH_RES_CLOCK_H

#include "qtypes.h"
#pragma once
#include <chrono>

class HighResClock {
public:
    HighResClock() = default;

    qint64 currentTime() const {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }
};

#endif // HIGH_RES_CLOCK_H
