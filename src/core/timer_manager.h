#pragma once
#include <Arduino.h>
#include "config.h"

typedef void (*TimerCallback)(uint8_t id);

class TimerManager {
public:
    static TimerManager& instance() {
        static TimerManager inst;
        return inst;
    }

    uint8_t setInterval(uint32_t intervalMs, TimerCallback cb);
    uint8_t setTimeout(uint32_t delayMs, TimerCallback cb);
    void cancel(uint8_t id);
    void update();

private:
    TimerManager() {}

    struct Timer {
        bool active = false;
        bool repeating = false;
        uint32_t interval = 0;
        uint32_t nextFire = 0;
        TimerCallback callback = nullptr;
    };

    Timer timers_[MAX_TIMERS];
};
