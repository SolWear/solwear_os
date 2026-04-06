#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// ============================================================
// Color Theme (RGB565) — Dark theme with Solana accent
// ============================================================
namespace Theme {
    // Backgrounds
    constexpr uint16_t BG_PRIMARY    = 0x0000;  // Black
    constexpr uint16_t BG_SECONDARY  = 0x18E3;  // Dark gray
    constexpr uint16_t BG_CARD       = 0x2124;  // Slightly lighter
    constexpr uint16_t BG_OVERLAY    = 0x0841;  // Very dark overlay

    // Text
    constexpr uint16_t TEXT_PRIMARY   = 0xFFFF;  // White
    constexpr uint16_t TEXT_SECONDARY = 0xB596;  // Light gray
    constexpr uint16_t TEXT_MUTED     = 0x7BEF;  // Medium gray

    // Accents
    constexpr uint16_t ACCENT        = 0x07FF;  // Cyan
    constexpr uint16_t ACCENT_SOL    = 0x9C1F;  // Solana purple (#9945FF)
    constexpr uint16_t ACCENT_GREEN  = 0x17E8;  // Solana green (#14F195)

    // Status colors
    constexpr uint16_t SUCCESS       = 0x07E0;  // Green
    constexpr uint16_t WARNING       = 0xFD20;  // Orange
    constexpr uint16_t DANGER        = 0xF800;  // Red
    constexpr uint16_t INFO          = 0x04DF;  // Blue

    // Battery colors
    constexpr uint16_t BAT_FULL      = 0x07E0;  // Green
    constexpr uint16_t BAT_MID       = 0xFD20;  // Orange
    constexpr uint16_t BAT_LOW       = 0xF800;  // Red
    constexpr uint16_t BAT_CHARGING  = 0x07FF;  // Cyan

    // Layout
    constexpr uint8_t PADDING        = 8;
    constexpr uint8_t CORNER_RADIUS  = 8;
    constexpr uint8_t SMALL_RADIUS   = 4;
}

// ============================================================
// Rect helper
// ============================================================
struct Rect {
    int16_t x, y;
    uint16_t w, h;

    bool contains(int16_t px, int16_t py) const {
        return px >= x && px < x + (int16_t)w && py >= y && py < y + (int16_t)h;
    }
};

// ============================================================
// Drawing helpers
// ============================================================
namespace Draw {
    inline void roundRect(TFT_eSprite& canvas, int16_t x, int16_t y,
                          uint16_t w, uint16_t h, uint8_t r, uint16_t color) {
        canvas.fillRoundRect(x, y, w, h, r, color);
    }

    inline void roundRectOutline(TFT_eSprite& canvas, int16_t x, int16_t y,
                                  uint16_t w, uint16_t h, uint8_t r, uint16_t color) {
        canvas.drawRoundRect(x, y, w, h, r, color);
    }

    // Draw a battery icon (procedural)
    void drawBatteryIcon(TFT_eSprite& canvas, int16_t x, int16_t y, uint8_t percent, bool charging);

    // Draw a simple centered text
    void drawCenteredText(TFT_eSprite& canvas, const char* text, int16_t y, uint8_t font, uint16_t color);
}
