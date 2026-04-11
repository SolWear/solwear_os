#include "hal_thermal.h"

HalThermal thermal;

void HalThermal::init() {
    update();
}

void HalThermal::update() {
    // RP2040 has a direct internal temperature API; other targets keep a
    // conservative placeholder until a board-specific sensor path is added.
#if defined(ARDUINO_ARCH_RP2040)
    float t = analogReadTemp();
#else
    float t = tempC_;
#endif
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
