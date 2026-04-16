#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

enum class GestureType : uint8_t {
    NONE = 0,
    TAP,
    LONG_PRESS,
    SWIPE_UP,
    SWIPE_DOWN,
    SWIPE_LEFT,
    SWIPE_RIGHT
};

struct TouchEvent {
    GestureType gesture;
    int16_t x, y;       // final position
    int16_t startX, startY;
    int16_t dx, dy;
    uint32_t timestamp;
};

class HalTouch {
public:
    void init();
    bool poll(TouchEvent& event);
    bool isTouching() const { return touching_; }
    int16_t getX() const { return lastX_; }
    int16_t getY() const { return lastY_; }

private:
    bool readRaw(int16_t& x, int16_t& y);
    GestureType classifyGesture();

    bool touching_ = false;
    bool wasTouching_ = false;
    int16_t lastX_ = 0, lastY_ = 0;
    int16_t startX_ = 0, startY_ = 0;
    uint32_t touchStartTime_ = 0;
    uint32_t lastPollTime_ = 0;
    
#if SOLWEAR_HAS_BUTTONS
    bool lastUp_ = true;
    bool lastDown_ = true;
    bool lastHash_ = true;
    bool lastStar_ = true;
#endif
};

extern HalTouch touch;
