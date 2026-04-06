#pragma once
#include "../core/app_framework.h"

class AlarmApp : public App {
public:
    void onCreate() override;
    void onDestroy() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    AppId getAppId() const override { return APP_ALARM; }

private:
    void renderAlarmSet(TFT_eSprite& canvas);
    void renderStopwatch(TFT_eSprite& canvas);

    uint8_t view_ = 0;  // 0=alarm, 1=stopwatch

    // Alarm
    uint8_t alarmHour_ = 7;
    uint8_t alarmMin_ = 0;
    bool alarmEnabled_ = false;
    bool alarmFiring_ = false;

    // Stopwatch
    bool swRunning_ = false;
    uint32_t swStartTime_ = 0;
    uint32_t swElapsed_ = 0;
    uint32_t swLaps_[5] = {};
    uint8_t swLapCount_ = 0;
};
