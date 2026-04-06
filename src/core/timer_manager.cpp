#include "timer_manager.h"

uint8_t TimerManager::setInterval(uint32_t intervalMs, TimerCallback cb) {
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (!timers_[i].active) {
            timers_[i].active = true;
            timers_[i].repeating = true;
            timers_[i].interval = intervalMs;
            timers_[i].nextFire = millis() + intervalMs;
            timers_[i].callback = cb;
            return i;
        }
    }
    return 0xFF;  // No slot available
}

uint8_t TimerManager::setTimeout(uint32_t delayMs, TimerCallback cb) {
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (!timers_[i].active) {
            timers_[i].active = true;
            timers_[i].repeating = false;
            timers_[i].interval = delayMs;
            timers_[i].nextFire = millis() + delayMs;
            timers_[i].callback = cb;
            return i;
        }
    }
    return 0xFF;
}

void TimerManager::cancel(uint8_t id) {
    if (id < MAX_TIMERS) {
        timers_[id].active = false;
    }
}

void TimerManager::update() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (timers_[i].active && now >= timers_[i].nextFire) {
            if (timers_[i].callback) {
                timers_[i].callback(i);
            }
            if (timers_[i].repeating) {
                timers_[i].nextFire = now + timers_[i].interval;
            } else {
                timers_[i].active = false;
            }
        }
    }
}
