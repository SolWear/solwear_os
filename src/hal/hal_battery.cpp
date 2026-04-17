#include "hal_battery.h"

HalBattery battery;

uint16_t HalBattery::readStableRawAdc_() {
    // VSYS/3 on GP29 is a relatively high-impedance source; the first read can
    // be biased low. Throw away one conversion, then average several samples.
    (void)analogRead(PIN_BATTERY_ADC);
    uint32_t sum = 0;
    constexpr uint8_t kSamples = 8;
    for (uint8_t i = 0; i < kSamples; ++i) {
        sum += analogRead(PIN_BATTERY_ADC);
        delayMicroseconds(150);
    }
    return (uint16_t)(sum / kSamples);
}

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
#if defined(ARDUINO_ARCH_RP2040)
    analogReadResolution(12);  // 12-bit ADC (0-4095)
#elif defined(ESP32)
    // ESP32-S3: 12-bit default; use 11dB attenuation so 0-3.3V maps to 0-4095.
    analogReadResolution(12);
#ifdef PIN_BATTERY_ADC
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
#endif
#endif
    update();
}

void HalBattery::update() {
#if SOLWEAR_HAS_BATTERY
#if defined(ESP8266) || defined(ESP32)
    rawAdc_ = analogRead(PIN_BATTERY_ADC);
    float v = (rawAdc_ / 4095.0f) * 3.3f * divider_; // ESP32 ADC is 12-bit by default
#else
    rawAdc_ = readStableRawAdc_();
    float v = (rawAdc_ / 4095.0f) * 3.3f * divider_;
#endif
#else
    rawAdc_ = 0;
    float v = 4.2f;
#endif

    if (firstRead_) {
        smoothedAdc_ = v;
        firstRead_ = false;
    } else {
        // Exponential moving average (alpha = 0.1)
        smoothedAdc_ = smoothedAdc_ * 0.9f + v * 0.1f;
    }

    voltage_ = smoothedAdc_;
    percent_ = voltageToPercent(voltage_);

    // Debounced charging detection: flip only after 5 consecutive agreeing samples.
    bool sample = (voltage_ > 4.25f);
    if (sample == chargingCandidate_) {
        if (chargingDebounce_ < 5) chargingDebounce_++;
        if (chargingDebounce_ >= 5 && charging_ != sample) {
            charging_ = sample;
        }
    } else {
        chargingCandidate_ = sample;
        chargingDebounce_ = 1;
    }
}

float HalBattery::calibrate(float measuredVoltage) {
    if (rawAdc_ == 0 || measuredVoltage <= 0.1f) return 0.0f;
    // measured = (raw/4095) * 3.3 * divider  →  divider = measured / ((raw/4095)*3.3)
    float adcVolts = (rawAdc_ / 4095.0f) * 3.3f;
    if (adcVolts < 0.05f) return 0.0f;
    float newDivider = measuredVoltage / adcVolts;
    if (newDivider < 0.5f || newDivider > 16.0f) return 0.0f;
    divider_ = newDivider;
    // Force the smoothing filter to re-converge from the new reading.
    firstRead_ = true;
    update();
    return divider_;
}
