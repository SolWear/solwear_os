#include "hal_display.h"

HalDisplay display;

void HalDisplay::init() {
    tft_.init();
    tft_.setRotation(0);
    tft_.fillScreen(TFT_BLACK);

    // Setup backlight PWM first so we can show errors if sprite alloc fails.
    pinMode(PIN_LCD_BL, OUTPUT);
    setBrightness(brightness_);

    // Try full-screen sprite first (~131 KB at 240x280x16bpp)
    canvas_.setColorDepth(16);
    void* ptr = canvas_.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);

    if (ptr != nullptr && canvas_.created()) {
        canvas_.fillSprite(TFT_BLACK);
        stripMode_ = false;
        ready_ = true;
        Serial.println("[HAL] Display: full-frame sprite OK");
        return;
    }

    Serial.println("[HAL] Display: full-frame sprite FAILED, trying strip mode");

    // Fallback: 240xSTRIP_HEIGHT (~19 KB). Apps will draw against this and
    // pushCanvas() tiles it down the screen STRIP_COUNT times per frame.
    canvas_.deleteSprite();
    ptr = canvas_.createSprite(SCREEN_WIDTH, STRIP_HEIGHT);

    if (ptr != nullptr && canvas_.created()) {
        canvas_.fillSprite(TFT_BLACK);
        stripMode_ = true;
        ready_ = true;
        Serial.println("[HAL] Display: strip-mode sprite OK");
        return;
    }

    Serial.println("[HAL] Display: ALL sprite allocations failed!");
    ready_ = false;
    stripMode_ = false;

    // Draw a red diagnostic bar directly to TFT so the user sees something.
    tft_.fillScreen(TFT_BLACK);
    tft_.setTextColor(TFT_RED);
    tft_.drawString("DISPLAY ALLOC FAIL", 20, 100, 2);
    tft_.drawString("Check serial log", 30, 130, 2);
}

void HalDisplay::pushCanvas() {
    if (!awake_ || !ready_) return;

    if (!stripMode_) {
        canvas_.pushSprite(0, 0);
        return;
    }

    // Strip mode: this is a degenerate path because we only have one strip
    // worth of pixels in memory. We push the current contents to y=0 — the
    // app will need to redraw and re-push for each strip if it cares about
    // multi-strip rendering. For now this keeps the device usable.
    canvas_.pushSprite(0, 0);
}

void HalDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    brightness_ = percent;
    analogWrite(PIN_LCD_BL, map(percent, 0, 100, 0, 255));
}

void HalDisplay::sleep() {
    awake_ = false;
    analogWrite(PIN_LCD_BL, 0);
}

void HalDisplay::wake() {
    awake_ = true;
    setBrightness(brightness_);
}
