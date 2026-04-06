#include "status_bar.h"
#include "../core/system_clock.h"
#include "../hal/hal_battery.h"

StatusBar statusBar;

void StatusBar::render(TFT_eSprite& canvas) {
    // Background
    canvas.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, Theme::BG_PRIMARY);

    // Time on the left
    char timeBuf[6];
    SystemClock::instance().formatTime(timeBuf, sizeof(timeBuf));
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString(timeBuf, 6, 4, 2);

    // Title in center (if set)
    if (title_) {
        int16_t tw = canvas.textWidth(title_, 2);
        canvas.drawString(title_, (SCREEN_WIDTH - tw) / 2, 4, 2);
    }

    // Battery on the right
    uint8_t batPercent = battery.getPercent();
    bool charging = battery.isCharging();
    Draw::drawBatteryIcon(canvas, SCREEN_WIDTH - 30, 4, batPercent, charging);

    // Battery percentage text
    char batBuf[5];
    snprintf(batBuf, sizeof(batBuf), "%d%%", batPercent);
    int16_t batTextW = canvas.textWidth(batBuf, 1);
    canvas.setTextColor(Theme::TEXT_SECONDARY);
    canvas.drawString(batBuf, SCREEN_WIDTH - 32 - batTextW, 6, 1);

    // Bottom line separator
    canvas.drawFastHLine(0, STATUS_BAR_HEIGHT - 1, SCREEN_WIDTH, Theme::BG_SECONDARY);
}
