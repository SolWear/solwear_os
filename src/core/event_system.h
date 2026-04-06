#pragma once
#include <Arduino.h>
#include "config.h"
#include "../hal/hal_touch.h"

enum class EventType : uint8_t {
    TOUCH,
    TICK,
    TIMER,
    STEP_DETECTED,
    NFC_TAG_FOUND,
    BATTERY_UPDATE,
    SYSTEM_SLEEP,
    SYSTEM_WAKE
};

struct Event {
    EventType type;
    uint32_t timestamp;
    union {
        TouchEvent touch;
        struct { uint32_t totalSteps; } step;
        struct { uint8_t percent; float voltage; } battery;
        struct { uint8_t uid[7]; uint8_t uidLen; } nfc;
        struct { uint8_t timerId; } timer;
    };
};

class EventSystem {
public:
    static EventSystem& instance() {
        static EventSystem inst;
        return inst;
    }

    bool post(const Event& event);
    bool poll(Event& event);
    bool isEmpty() const { return head_ == tail_; }
    void clear() { head_ = tail_ = 0; }

private:
    EventSystem() {}
    Event queue_[EVENT_QUEUE_SIZE];
    volatile uint8_t head_ = 0;
    volatile uint8_t tail_ = 0;
};
