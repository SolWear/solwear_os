#pragma once
#include "../core/app_framework.h"
#include "../hal/hal_nfc.h"

enum class NfcMode : uint8_t {
    IDLE,
    SCANNING,
    TAG_FOUND,
    WRITING,
    WRITE_OK,
    WRITE_FAIL,
    CONTRACT_TEST
};

class NfcApp : public App {
public:
    void onCreate() override;
    void onDestroy() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    AppId getAppId() const override { return APP_NFC; }

private:
    void renderIdle(TFT_eSprite& canvas);
    void renderScanning(TFT_eSprite& canvas);
    void renderTagFound(TFT_eSprite& canvas);
    void renderWriting(TFT_eSprite& canvas);
    void renderResult(TFT_eSprite& canvas, bool success);
    void renderContractTest(TFT_eSprite& canvas);

    NfcMode mode_ = NfcMode::IDLE;
    NfcTagInfo tagInfo_ = {};
    char ndefData_[128] = "";
    uint32_t animTimer_ = 0;
    uint8_t animFrame_ = 0;
    uint32_t resultTime_ = 0;
    
#if SOLWEAR_HAS_BUTTONS
    int8_t selectedIndex_ = 0;
#endif
};
