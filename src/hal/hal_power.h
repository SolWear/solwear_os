#pragma once
#include <Arduino.h>
#include "config.h"

enum class PowerState : uint8_t {
    ACTIVE,     // Full brightness, 30fps
    DIMMED,     // Reduced brightness, 30fps
    SLEEP       // Display off, minimal processing
};

class HalPower {
public:
    void init();
    void update();
    void registerActivity();
    PowerState getState() const { return state_; }
    bool isDisplayOn() const { return state_ != PowerState::SLEEP; }

private:
    void transitionTo(PowerState newState);

    PowerState state_ = PowerState::ACTIVE;
    uint32_t lastActivityTime_ = 0;
    uint8_t savedBrightness_ = BRIGHTNESS_DEFAULT;
};

extern HalPower power;
