#include "hal_power.h"
#include "hal_display.h"
#if defined(ARDUINO_ARCH_RP2040)
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#endif

HalPower power;

// Keep a fixed clock in diagnostics firmware to avoid USB CDC instability
// seen when dynamically changing RP2040 sys clock.
static constexpr uint32_t CLK_NORMAL_KHZ   = 64000;

void HalPower::init() {
    lastActivityTime_ = millis();
    state_ = PowerState::ACTIVE;
}

void HalPower::holdOn() {
    const uint8_t latchPins[] = {14, 15, 18, 19};
    for (uint8_t pin : latchPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

void HalPower::holdOff() {
    // WARNING: this kills the 3.3V rail. The MCU will lose power.
    const uint8_t latchPins[] = {14, 15, 18, 19};
    for (uint8_t pin : latchPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
}

void HalPower::onTemperatureUpdate(float tempC) {
    // In this recovery phase we only clamp brightness at high temperature.
    // Dynamic clock switching caused COM instability on this board.
    if (tempC > 65.0f) {
        if (state_ != PowerState::SLEEP) {
            transitionTo(PowerState::SLEEP);
        }
#if !SOLWEAR_EINK_TARGET
    } else if (tempC > 55.0f && !throttled_) {
        throttled_ = true;
        display.setBrightness(30);
    } else if (tempC < 50.0f && throttled_) {
        throttled_ = false;
        display.setBrightness(savedBrightness_);
#endif
    }
}

void HalPower::registerActivity() {
    lastActivityTime_ = millis();
    if (state_ != PowerState::ACTIVE) {
        transitionTo(PowerState::ACTIVE);
    }
}

void HalPower::setDiagnosticsMode(bool enabled) {
    diagnosticsMode_ = enabled;
    if (diagnosticsMode_) {
#if defined(ARDUINO_ARCH_RP2040)
        set_sys_clock_khz(CLK_NORMAL_KHZ, true);
#endif
#if !SOLWEAR_EINK_TARGET
        display.wake();
        display.setBrightness(100);
#endif
        lastActivityTime_ = millis();
    } else {
#if !SOLWEAR_EINK_TARGET
        display.setBrightness(savedBrightness_);
#endif
        lastActivityTime_ = millis();
    }
}

void HalPower::update() {
    if (diagnosticsMode_) {
        // Hold the panel fully on while probing hardware.
        lastActivityTime_ = millis();
        return;
    }

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
            // Restore clock first so the rest of init runs at full speed.
#if defined(ARDUINO_ARCH_RP2040)
            set_sys_clock_khz(CLK_NORMAL_KHZ, true);
#endif
#if !SOLWEAR_EINK_TARGET
            display.wake();
            display.setBrightness(savedBrightness_);
#endif
            break;

        case PowerState::DIMMED:
#if !SOLWEAR_EINK_TARGET
            savedBrightness_ = display.getBrightness();
            display.setBrightness(BRIGHTNESS_DIM);
#endif
            // Stay at normal clock for snappy wake; just dim the screen.
            break;

        case PowerState::SLEEP:
#if !SOLWEAR_EINK_TARGET
            display.sleep();
#endif
            break;
    }

    state_ = newState;
}
