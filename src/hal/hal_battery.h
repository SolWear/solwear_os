#pragma once
#include <Arduino.h>
#include "config.h"

class HalBattery {
public:
    void init();
    void update();
    float getVoltage() const { return voltage_; }
    uint8_t getPercent() const { return percent_; }
    bool isLow() const { return percent_ <= 10; }
    bool isCritical() const { return percent_ <= 5; }
    bool isCharging() const { return charging_; }

private:
    float voltage_ = 4.2f;
    uint8_t percent_ = 100;
    float smoothedAdc_ = 0;
    bool charging_ = false;
    bool firstRead_ = true;
};

extern HalBattery battery;
