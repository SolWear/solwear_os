#pragma once
#include "../core/app_framework.h"

// Charging overlay app — pushed when USB power is detected, popped when removed.
// Renders an animated charging bolt + battery fill animation.
class ChargingApp : public App {
public:
    void onCreate() override;
    void onDestroy() override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    void onEvent(const Event& event) override;

    bool wantsStatusBar() override { return false; }
    bool handleBackGesture() override { return true; }  // can't dismiss manually
    AppId getAppId() const override { return APP_CHARGING; }

private:
    uint32_t animMs_ = 0;       // animation timer
    float fillPhase_ = 0.0f;    // 0..1 sine sweep for battery fill
    uint8_t lastPercent_ = 0;
    float displayVolts_ = 0.0f;
};
