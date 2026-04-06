#pragma once
#include "../core/app_framework.h"
#include "../core/storage.h"

class SettingsApp : public App {
public:
    void onCreate() override;
    void onDestroy() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    AppId getAppId() const override { return APP_SETTINGS; }

private:
    static constexpr uint8_t ITEM_COUNT = 7;
    static constexpr uint8_t ITEM_HEIGHT = 36;

    void renderItem(TFT_eSprite& canvas, uint8_t idx, int16_t y);

    Settings settings_;
    int8_t scrollOffset_ = 0;
    int8_t selectedItem_ = -1;
};
