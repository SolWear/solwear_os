#include "hal_thermal.h"

HalThermal thermal;

void HalThermal::init() {
    update();
}

void HalThermal::update() {
    // analogReadTemp() is provided by the earlephilhower RP2040 Arduino core
    // and reads ADC4 (the chip's internal temperature sensor) returning C.
    float t = analogReadTemp();
    if (t < -20.0f || t > 120.0f) {
        // Out-of-range reading — ignore.
        return;
    }
    if (firstRead_) {
        tempC_ = t;
        firstRead_ = false;
    } else {
        // EMA with alpha=0.2 for slightly faster response than the battery filter.
        tempC_ = tempC_ * 0.8f + t * 0.2f;
    }
}
