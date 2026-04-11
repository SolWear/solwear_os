#pragma once
#include <TFT_eSPI.h>
#include "config.h"

// Strip mode renders the screen in N horizontal bands. We allocate a single
// strip-sized sprite and reuse it. This is the fallback when the full
// 240x280x16bpp sprite (~131 KB) can't be allocated.
#define STRIP_HEIGHT 40
#define STRIP_COUNT  ((SCREEN_HEIGHT + STRIP_HEIGHT - 1) / STRIP_HEIGHT)

class HalDisplay {
public:
    void init();
    bool isReady() const { return ready_; }
    bool isStripMode() const { return stripMode_; }

    // Returns a sprite the size of the full screen (or strip in fallback).
    // App code calls fillSprite/draw* on this and then pushCanvas().
    TFT_eSprite& getCanvas() { return canvas_; }

    void pushCanvas();
    void runPanelProbe();
    void runPanelSweep();
    void setBrightness(uint8_t percent);
    uint8_t getBrightness() const { return brightness_; }
    void sleep();
    void wake();
    bool isAwake() const { return awake_; }

private:
    TFT_eSPI tft_;
    TFT_eSprite canvas_ = TFT_eSprite(&tft_);
    uint8_t brightness_ = BRIGHTNESS_DEFAULT;
    bool awake_ = true;
    bool ready_ = false;       // tft_ + sprite both initialized successfully
    bool stripMode_ = false;   // true => sprite is one strip; pushCanvas tiles it
    bool lowColorMode_ = false; // true => full-frame sprite is 8-bit (fallback)
};

extern HalDisplay display;
