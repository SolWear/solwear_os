#pragma once
#include "../core/app_framework.h"

class HomeApp : public App {
public:
    void onCreate() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    bool wantsStatusBar() override { return false; }
    AppId getAppId() const override { return APP_HOME; }

private:
    void drawIcon(TFT_eSprite& canvas, uint8_t index, int16_t x, int16_t y, bool highlight);

    int8_t highlightIdx_ = -1;
    uint32_t highlightTime_ = 0;
    uint8_t wallpaperIdx_ = 0;
};
