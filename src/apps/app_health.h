#pragma once
#include "../core/app_framework.h"
#include "../core/storage.h"

class HealthApp : public App {
public:
    void onCreate() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    AppId getAppId() const override { return APP_HEALTH; }

private:
    void renderToday(TFT_eSprite& canvas);
    void renderHistory(TFT_eSprite& canvas);
    void drawProgressRing(TFT_eSprite& canvas, int16_t cx, int16_t cy, int16_t r,
                          float progress, uint16_t color);

    uint8_t view_ = 0;  // 0=today, 1=history
    uint16_t stepGoal_ = DEFAULT_STEP_GOAL;
    uint32_t weekSteps_[7] = {};
};
