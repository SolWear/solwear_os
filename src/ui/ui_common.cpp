#include "ui_common.h"

namespace Draw {

void drawBatteryIcon(TFT_eSprite& canvas, int16_t x, int16_t y, uint8_t percent, bool charging) {
    // Battery body: 20x10 pixels
    uint16_t bodyColor = Theme::TEXT_SECONDARY;
    uint16_t fillColor;

    if (charging) {
        fillColor = Theme::BAT_CHARGING;
    } else if (percent > 50) {
        fillColor = Theme::BAT_FULL;
    } else if (percent > 20) {
        fillColor = Theme::BAT_MID;
    } else {
        fillColor = Theme::BAT_LOW;
    }

    // Outline
    canvas.drawRect(x, y + 2, 20, 10, bodyColor);
    // Terminal
    canvas.fillRect(x + 20, y + 4, 2, 6, bodyColor);

    // Fill proportional to percent
    uint8_t fillW = (uint8_t)((percent / 100.0f) * 18);
    if (fillW > 18) fillW = 18;
    if (fillW > 0) {
        canvas.fillRect(x + 1, y + 3, fillW, 8, fillColor);
    }

    // Charging bolt symbol
    if (charging) {
        canvas.drawLine(x + 10, y + 3, x + 7, y + 7, Theme::BG_PRIMARY);
        canvas.drawLine(x + 7, y + 7, x + 12, y + 7, Theme::BG_PRIMARY);
        canvas.drawLine(x + 12, y + 7, x + 9, y + 11, Theme::BG_PRIMARY);
    }
}

void drawCenteredText(TFT_eSprite& canvas, const char* text, int16_t y, uint8_t font, uint16_t color) {
    int16_t tw = canvas.textWidth(text, font);
    canvas.setTextColor(color);
    canvas.drawString(text, (SCREEN_WIDTH - tw) / 2, y, font);
}

}  // namespace Draw
