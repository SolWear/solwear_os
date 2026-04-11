#pragma once
#include <Arduino.h>

// Thin wrapper around the RP2040 internal temperature sensor (ADC4).
// Uses the earlephilhower core's analogReadTemp() helper which returns
// degrees C as a float. Cheap to call — poll once per second.
class HalThermal {
public:
    void init();
    void update();

    float getTemperatureC() const { return tempC_; }
    bool isWarm() const     { return tempC_ > 40.0f; }
    bool isHot() const      { return tempC_ > 50.0f; }
    bool isCritical() const { return tempC_ > 65.0f; }

private:
    float tempC_ = 0.0f;
    bool firstRead_ = true;
};

extern HalThermal thermal;
