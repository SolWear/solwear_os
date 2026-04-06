#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

struct ImuData {
    float ax, ay, az;   // acceleration in g
    float gx, gy, gz;   // gyroscope in dps
};

class HalImu {
public:
    void init();
    bool read(ImuData& data);
    bool detectStep();
    uint32_t getSteps() const { return stepCount_; }
    void resetSteps() { stepCount_ = 0; }

private:
    void writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
    void readRegs(uint8_t reg, uint8_t* buf, uint8_t len);

    uint32_t stepCount_ = 0;
    float lastMagnitude_ = 0;
    bool aboveThreshold_ = false;
    uint32_t lastStepTime_ = 0;
};

extern HalImu imu;
