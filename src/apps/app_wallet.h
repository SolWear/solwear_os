#pragma once
#include "../core/app_framework.h"
#include "../core/storage.h"

class WalletApp : public App {
public:
    void onCreate() override;
    void onEvent(const Event& event) override;
    void render(TFT_eSprite& canvas) override;
    AppId getAppId() const override { return APP_WALLET; }

private:
    void renderAddress(TFT_eSprite& canvas);
    void renderBalance(TFT_eSprite& canvas);
    void renderHistory(TFT_eSprite& canvas);

    WalletData wallet_;
    uint8_t view_ = 0;
};
