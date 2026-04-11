#pragma once
#include "../core/app_framework.h"
#include "../core/system_clock.h"
#include "../hal/hal_imu.h"

enum class WatchFaceStyle : uint8_t {
    DIGITAL = 0,
    ANALOG_FACE,
    MINIMAL,
    SOLANA,
    STYLE_COUNT
};

class WatchFaceApp : public App {
public:
    void onCreate() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    bool wantsStatusBar() override { return false; }
    bool handleBackGesture() override { return true; }  // Don't pop watchface
    AppId getAppId() const override { return APP_WATCHFACE; }

    void setStyle(WatchFaceStyle s) {
        style_ = (WatchFaceStyle)((uint8_t)s % (uint8_t)WatchFaceStyle::STYLE_COUNT);
    }
    void setWallpaperIndex(uint8_t idx) { wallpaperIdx_ = idx; }

private:
    void formatClock(char* buf, size_t len, bool withSeconds = false) const;
    void renderDigital(TFT_eSprite& canvas);
    void renderAnalog(TFT_eSprite& canvas);
    void renderMinimal(TFT_eSprite& canvas);
    void renderSolana(TFT_eSprite& canvas);

    WatchFaceStyle style_ = WatchFaceStyle::DIGITAL;
    bool use24Hour_ = true;
    uint8_t wallpaperIdx_ = 0;
    uint32_t lastSecond_ = 0;
    uint32_t settingsPollMs_ = 0;
};
