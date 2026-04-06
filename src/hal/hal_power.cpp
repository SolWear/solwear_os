#include "hal_power.h"
#include "hal_display.h"

HalPower power;

void HalPower::init() {
    lastActivityTime_ = millis();
    state_ = PowerState::ACTIVE;
}

void HalPower::registerActivity() {
    lastActivityTime_ = millis();
    if (state_ != PowerState::ACTIVE) {
        transitionTo(PowerState::ACTIVE);
    }
}

void HalPower::update() {
    uint32_t elapsed = millis() - lastActivityTime_;

    switch (state_) {
        case PowerState::ACTIVE:
            if (elapsed >= DISPLAY_TIMEOUT_MS) {
                transitionTo(PowerState::SLEEP);
            } else if (elapsed >= DIM_TIMEOUT_MS) {
                transitionTo(PowerState::DIMMED);
            }
            break;

        case PowerState::DIMMED:
            if (elapsed >= DISPLAY_TIMEOUT_MS) {
                transitionTo(PowerState::SLEEP);
            }
            break;

        case PowerState::SLEEP:
            // Wait for touch to wake (registerActivity)
            break;
    }
}

void HalPower::transitionTo(PowerState newState) {
    if (newState == state_) return;

    switch (newState) {
        case PowerState::ACTIVE:
            display.wake();
            display.setBrightness(savedBrightness_);
            break;

        case PowerState::DIMMED:
            savedBrightness_ = display.getBrightness();
            display.setBrightness(BRIGHTNESS_DIM);
            break;

        case PowerState::SLEEP:
            display.sleep();
            break;
    }

    state_ = newState;
}
