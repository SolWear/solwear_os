#pragma once
#include <Arduino.h>
#include "config.h"

class HalBattery {
public:
    void init();
    void update();
    float getVoltage() const { return voltage_; }
    uint16_t getRawAdc() const { return rawAdc_; }
    uint8_t getPercent() const { return percent_; }
    bool isLow() const { return percent_ <= 10; }
    bool isCritical() const { return percent_ <= 5; }
    bool isCharging() const { return charging_; }

    // Runtime-tunable divider — persisted via Storage/Settings.
    void setDivider(float d) { if (d > 0.5f && d < 16.0f) divider_ = d; }
    float getDivider() const { return divider_; }

    // Calibrate against a measured battery voltage. Computes and stores
    // a new divider based on the current raw ADC reading.
    // Returns the new divider value (or 0 if the raw ADC is unusable).
    float calibrate(float measuredVoltage);

private:
    uint16_t readStableRawAdc_();

    float voltage_ = 4.2f;
    uint16_t rawAdc_ = 0;
    uint8_t percent_ = 100;
    float smoothedAdc_ = 0;
    float divider_ = BATTERY_DIVIDER;
    bool charging_ = false;
    bool firstRead_ = true;

    // Charging detection debounce — flip only when 5 consecutive readings agree.
    uint8_t chargingDebounce_ = 0;
    bool chargingCandidate_ = false;
};

extern HalBattery battery;
