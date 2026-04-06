#pragma once
#include <Arduino.h>

// ============================================================
// Procedurally generated placeholder icons (48x48 RGB565)
// These will be drawn at runtime to save flash space
// ============================================================

// App icon colors (RGB565)
#define ICON_COLOR_SETTINGS  0x7BEF  // Gray
#define ICON_COLOR_WALLET    0x9C1F  // Purple (Solana)
#define ICON_COLOR_NFC       0x07FF  // Cyan
#define ICON_COLOR_HEALTH    0x07E0  // Green
#define ICON_COLOR_GAME      0xFD20  // Orange
#define ICON_COLOR_ALARM     0xF800  // Red
#define ICON_COLOR_CLOCK     0x04DF  // Blue
#define ICON_COLOR_STATS     0x17E8  // Solana green

namespace Icons {

// Draw a gear icon (Settings)
inline void drawSettings(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_SETTINGS);
    // Gear shape: circle with notches
    canvas.fillCircle(x + 24, y + 24, 12, 0x4208);
    canvas.fillCircle(x + 24, y + 24, 7, ICON_COLOR_SETTINGS);
    // Gear teeth (simplified as small rects)
    for (int i = 0; i < 8; i++) {
        float angle = i * 0.785f;  // 45 degrees
        int16_t tx = x + 24 + (int16_t)(14 * cosf(angle));
        int16_t ty = y + 24 + (int16_t)(14 * sinf(angle));
        canvas.fillRect(tx - 2, ty - 2, 5, 5, 0x4208);
    }
}

// Draw a wallet icon (Wallet / Solana)
inline void drawWallet(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_WALLET);
    // Solana logo: three parallel lines
    canvas.drawLine(x + 12, y + 16, x + 36, y + 16, 0xFFFF);
    canvas.drawLine(x + 36, y + 16, x + 32, y + 20, 0xFFFF);
    canvas.drawLine(x + 12, y + 24, x + 36, y + 24, 0xFFFF);
    canvas.drawLine(x + 12, y + 24, x + 16, y + 20, 0xFFFF);
    canvas.drawLine(x + 12, y + 32, x + 36, y + 32, 0xFFFF);
    canvas.drawLine(x + 36, y + 32, x + 32, y + 36, 0xFFFF);
}

// Draw NFC icon (radio waves)
inline void drawNfc(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_NFC);
    // NFC symbol: circle + arcs
    canvas.fillCircle(x + 20, y + 28, 4, 0x0000);
    canvas.drawCircle(x + 20, y + 28, 9, 0x0000);
    canvas.drawCircle(x + 20, y + 28, 14, 0x0000);
    canvas.drawCircle(x + 20, y + 28, 19, 0x0000);
    // "N" letter
    canvas.setTextColor(0x0000);
    canvas.drawString("N", x + 32, y + 8, 2);
}

// Draw health icon (heart)
inline void drawHealth(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_HEALTH);
    // Simple heart shape using circles and triangle
    canvas.fillCircle(x + 18, y + 20, 7, 0xFFFF);
    canvas.fillCircle(x + 30, y + 20, 7, 0xFFFF);
    canvas.fillTriangle(x + 11, y + 22, x + 37, y + 22, x + 24, y + 38, 0xFFFF);
}

// Draw game icon (controller)
inline void drawGame(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_GAME);
    // D-pad
    canvas.fillRect(x + 10, y + 20, 12, 4, 0x0000);
    canvas.fillRect(x + 14, y + 16, 4, 12, 0x0000);
    // Buttons
    canvas.fillCircle(x + 32, y + 20, 3, 0x0000);
    canvas.fillCircle(x + 38, y + 26, 3, 0x0000);
    // Controller body
    canvas.drawRoundRect(x + 6, y + 12, 36, 24, 6, 0x0000);
}

// Draw stats icon (bar chart)
inline void drawStats(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_STATS);
    // Three bars of increasing height
    canvas.fillRect(x + 10, y + 28, 6, 12, 0x0000);
    canvas.fillRect(x + 21, y + 20, 6, 20, 0x0000);
    canvas.fillRect(x + 32, y + 12, 6, 28, 0x0000);
}

// Draw alarm icon (bell)
inline void drawAlarm(TFT_eSprite& canvas, int16_t x, int16_t y) {
    canvas.fillRoundRect(x, y, 48, 48, 10, ICON_COLOR_ALARM);
    // Bell shape
    canvas.fillTriangle(x + 14, y + 32, x + 34, y + 32, x + 24, y + 12, 0xFFFF);
    canvas.fillCircle(x + 24, y + 16, 6, 0xFFFF);
    canvas.fillRect(x + 14, y + 28, 20, 6, 0xFFFF);
    // Clapper
    canvas.fillCircle(x + 24, y + 36, 3, 0xFFFF);
}

}  // namespace Icons
