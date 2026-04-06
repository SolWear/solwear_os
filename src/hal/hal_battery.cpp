#include "hal_battery.h"

HalBattery battery;

// LiPo discharge curve (voltage -> percentage)
static uint8_t voltageToPercent(float v) {
    if (v >= 4.20f) return 100;
    if (v >= 4.10f) return 90;
    if (v >= 4.00f) return 80;
    if (v >= 3.90f) return 70;
    if (v >= 3.80f) return 60;
    if (v >= 3.70f) return 50;
    if (v >= 3.60f) return 40;
    if (v >= 3.50f) return 30;
    if (v >= 3.40f) return 20;
    if (v >= 3.30f) return 10;
    if (v >= 3.20f) return 5;
    return 0;
}

void HalBattery::init() {
    analogReadResolution(12);  // 12-bit ADC (0-4095)
    update();
}

void HalBattery::update() {
    uint16_t raw = analogRead(PIN_BATTERY_ADC);
    float v = (raw / 4095.0f) * 3.3f * BATTERY_DIVIDER;

    if (firstRead_) {
        smoothedAdc_ = v;
        firstRead_ = false;
    } else {
        // Exponential moving average (alpha = 0.1)
        smoothedAdc_ = smoothedAdc_ * 0.9f + v * 0.1f;
    }

    voltage_ = smoothedAdc_;
    percent_ = voltageToPercent(voltage_);

    // Simple charging detection: voltage above 4.2V suggests charger connected
    charging_ = (voltage_ > 4.25f);
}
