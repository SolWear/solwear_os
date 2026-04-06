#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// ============================================================
// Procedural Wallpapers — drawn at runtime (zero flash cost)
// ============================================================

namespace Wallpapers {

constexpr uint8_t COUNT = 5;

// 0: Solid black
inline void drawBlack(TFT_eSprite& canvas) {
    canvas.fillSprite(TFT_BLACK);
}

// 1: Deep space / starfield
inline void drawStarfield(TFT_eSprite& canvas) {
    canvas.fillSprite(0x0000);
    // Deterministic starfield using simple hash
    for (int i = 0; i < 120; i++) {
        uint16_t x = (i * 1103 + 277) % SCREEN_WIDTH;
        uint16_t y = (i * 947 + 613) % SCREEN_HEIGHT;
        uint8_t brightness = 100 + (i * 37) % 156;
        uint16_t color = canvas.color565(brightness, brightness, brightness);
        canvas.drawPixel(x, y, color);
        if (i % 5 == 0) {
            canvas.drawPixel(x + 1, y, color);  // Bigger stars
        }
    }
}

// 2: Solana gradient (purple -> teal)
inline void drawSolanaGradient(TFT_eSprite& canvas) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        float t = (float)y / SCREEN_HEIGHT;
        // From Solana purple (#9945FF) to Solana green (#14F195)
        uint8_t r = (uint8_t)(153 * (1 - t) + 20 * t);
        uint8_t g = (uint8_t)(69 * (1 - t) + 241 * t);
        uint8_t b = (uint8_t)(255 * (1 - t) + 149 * t);
        // Darken to 30% for readability
        r = r * 30 / 100;
        g = g * 30 / 100;
        b = b * 30 / 100;
        canvas.drawFastHLine(0, y, SCREEN_WIDTH, canvas.color565(r, g, b));
    }
}

// 3: Geometric grid (cyberpunk)
inline void drawCyberGrid(TFT_eSprite& canvas) {
    canvas.fillSprite(0x0000);
    // Horizontal grid lines (perspective effect)
    for (int y = SCREEN_HEIGHT / 2; y < SCREEN_HEIGHT; y += 12) {
        uint8_t brightness = map(y, SCREEN_HEIGHT / 2, SCREEN_HEIGHT, 20, 80);
        uint16_t color = canvas.color565(0, brightness, brightness);
        canvas.drawFastHLine(0, y, SCREEN_WIDTH, color);
    }
    // Vertical grid lines
    for (int x = 0; x < SCREEN_WIDTH; x += 20) {
        for (int y = SCREEN_HEIGHT / 2; y < SCREEN_HEIGHT; y++) {
            uint8_t brightness = map(y, SCREEN_HEIGHT / 2, SCREEN_HEIGHT, 10, 60);
            canvas.drawPixel(x, y, canvas.color565(0, brightness, brightness));
        }
    }
    // Sun/circle at horizon
    canvas.drawCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 40, canvas.color565(80, 20, 60));
    canvas.drawCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 38, canvas.color565(60, 15, 45));
}

// 4: Matrix rain (subtle green)
inline void drawMatrix(TFT_eSprite& canvas) {
    canvas.fillSprite(0x0000);
    // Columns of random-looking characters (deterministic)
    for (int col = 0; col < SCREEN_WIDTH; col += 12) {
        int len = 3 + (col * 7) % 12;
        int startY = (col * 13) % SCREEN_HEIGHT;
        for (int i = 0; i < len; i++) {
            int y = (startY + i * 14) % SCREEN_HEIGHT;
            uint8_t brightness = 255 - i * (200 / len);
            if (brightness < 30) brightness = 30;
            uint16_t color = canvas.color565(0, brightness / 3, 0);
            char c = '0' + ((col + i * 3) % 10);
            canvas.setTextColor(color);
            canvas.drawChar(c, col, y, 1);
        }
    }
}

// Draw wallpaper by index
inline void draw(TFT_eSprite& canvas, uint8_t index) {
    switch (index % COUNT) {
        case 0: drawBlack(canvas); break;
        case 1: drawStarfield(canvas); break;
        case 2: drawSolanaGradient(canvas); break;
        case 3: drawCyberGrid(canvas); break;
        case 4: drawMatrix(canvas); break;
    }
}

inline const char* getName(uint8_t index) {
    static const char* names[] = {"Black", "Starfield", "Solana", "Cyber", "Matrix"};
    return names[index % COUNT];
}

}  // namespace Wallpapers
