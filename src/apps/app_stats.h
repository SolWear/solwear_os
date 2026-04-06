#pragma once
#include "../core/app_framework.h"

// Stats app — two pages, swipe left/right to switch:
//   page 0: Battery (voltage, %, charging, est time)
//   page 1: System  (heap, uptime, fw version, CPU freq, steps)
class StatsApp : public App {
public:
    void onCreate() override;
    void onDestroy() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    AppId getAppId() const override { return APP_STATS; }

private:
    void renderBatteryPage(TFT_eSprite& canvas);
    void renderSystemPage(TFT_eSprite& canvas);

    uint8_t page_ = 0;        // 0 = battery, 1 = system
    uint32_t refreshMs_ = 0;  // throttle live updates
};
