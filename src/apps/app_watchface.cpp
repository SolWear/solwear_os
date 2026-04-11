#include "app_watchface.h"
#include "../core/screen_manager.h"
#include "../core/storage.h"
#include "../ui/ui_common.h"
#include "../assets/wallpapers.h"
#include "../hal/hal_battery.h"
#include <math.h>

void WatchFaceApp::onCreate() {
    Settings settings;
    if (Storage::instance().loadSettings(settings)) {
        style_ = (WatchFaceStyle)settings.watchFaceIndex;
        wallpaperIdx_ = settings.wallpaperIndex;
    }
}

void WatchFaceApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

    switch (event.touch.gesture) {
        case GestureType::SWIPE_UP:
            // Open home screen
            ScreenManager::instance().pushScreen(APP_HOME, Transition::SLIDE_UP);
            break;
        case GestureType::TAP:
            // Cycle watch face style
            style_ = (WatchFaceStyle)(((uint8_t)style_ + 1) % (uint8_t)WatchFaceStyle::STYLE_COUNT);
            break;
        default:
            break;
    }
}

void WatchFaceApp::update(uint32_t dt) {
    // Nothing complex needed — render handles time display
}

void WatchFaceApp::render(TFT_eSprite& canvas) {
    // Draw wallpaper
    Wallpapers::draw(canvas, wallpaperIdx_);

    switch (style_) {
        case WatchFaceStyle::DIGITAL:  renderDigital(canvas);  break;
        case WatchFaceStyle::ANALOG_FACE: renderAnalog(canvas); break;
        case WatchFaceStyle::MINIMAL:  renderMinimal(canvas);  break;
    }

    // Battery indicator (top right)
    Draw::drawBatteryIcon(canvas, SCREEN_WIDTH - 30, 6, battery.getPercent(), battery.isCharging());
}

void WatchFaceApp::renderDigital(TFT_eSprite& canvas) {
    SystemClock& clk = SystemClock::instance();
    char buf[16];

    // Large time
    clk.formatTime(buf, sizeof(buf));
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    Draw::drawCenteredText(canvas, buf, 90, 7, Theme::TEXT_PRIMARY);

    // Seconds
    DateTime dt = clk.now();
    snprintf(buf, sizeof(buf), "%02d", dt.second);
    Draw::drawCenteredText(canvas, buf, 145, 4, Theme::ACCENT);

    // Date
    clk.formatDate(buf, sizeof(buf));
    Draw::drawCenteredText(canvas, buf, 175, 2, Theme::TEXT_SECONDARY);

    // Step count
    uint32_t steps = imu.getSteps();
    snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)steps);
    Draw::drawCenteredText(canvas, buf, 230, 2, Theme::ACCENT_GREEN);
}

void WatchFaceApp::renderAnalog(TFT_eSprite& canvas) {
    int16_t cx = SCREEN_WIDTH / 2;
    int16_t cy = 140;
    int16_t radius = 95;

    // Hour markers
    for (int i = 0; i < 12; i++) {
        float angle = i * 30.0f * PI / 180.0f - PI / 2;
        int16_t x1 = cx + (int16_t)((radius - 8) * cosf(angle));
        int16_t y1 = cy + (int16_t)((radius - 8) * sinf(angle));
        int16_t x2 = cx + (int16_t)(radius * cosf(angle));
        int16_t y2 = cy + (int16_t)(radius * sinf(angle));
        canvas.drawLine(x1, y1, x2, y2, Theme::TEXT_SECONDARY);
    }

    // Circle outline
    canvas.drawCircle(cx, cy, radius, Theme::TEXT_SECONDARY);

    DateTime dt = SystemClock::instance().now();

    // Hour hand
    float hourAngle = ((dt.hour % 12) + dt.minute / 60.0f) * 30.0f * PI / 180.0f - PI / 2;
    int16_t hx = cx + (int16_t)(55 * cosf(hourAngle));
    int16_t hy = cy + (int16_t)(55 * sinf(hourAngle));
    canvas.drawLine(cx, cy, hx, hy, Theme::TEXT_PRIMARY);
    canvas.drawLine(cx + 1, cy, hx + 1, hy, Theme::TEXT_PRIMARY);

    // Minute hand
    float minAngle = dt.minute * 6.0f * PI / 180.0f - PI / 2;
    int16_t mx = cx + (int16_t)(75 * cosf(minAngle));
    int16_t my = cy + (int16_t)(75 * sinf(minAngle));
    canvas.drawLine(cx, cy, mx, my, Theme::ACCENT);

    // Second hand
    float secAngle = dt.second * 6.0f * PI / 180.0f - PI / 2;
    int16_t sx = cx + (int16_t)(80 * cosf(secAngle));
    int16_t sy = cy + (int16_t)(80 * sinf(secAngle));
    canvas.drawLine(cx, cy, sx, sy, Theme::DANGER);

    // Center dot
    canvas.fillCircle(cx, cy, 3, Theme::TEXT_PRIMARY);

    // Date at bottom
    char dateBuf[16];
    SystemClock::instance().formatDateShort(dateBuf, sizeof(dateBuf));
    Draw::drawCenteredText(canvas, dateBuf, 250, 2, Theme::TEXT_SECONDARY);
}

void WatchFaceApp::renderMinimal(TFT_eSprite& canvas) {
    SystemClock& clk = SystemClock::instance();
    DateTime dt = clk.now();

    // Time only, large centered
    char buf[6];
    clk.formatTime(buf, sizeof(buf));
    Draw::drawCenteredText(canvas, buf, 115, 7, Theme::TEXT_PRIMARY);

    // Activity ring around screen edge
    uint32_t steps = imu.getSteps();
    Settings settings;
    Storage::instance().loadSettings(settings);
    float progress = (float)steps / settings.stepGoal;
    if (progress > 1.0f) progress = 1.0f;

    // Draw arc (approximate with line segments)
    int16_t cx = SCREEN_WIDTH / 2;
    int16_t cy = SCREEN_HEIGHT / 2;
    int16_t r = 115;
    float startAngle = -PI / 2;
    float endAngle = startAngle + progress * 2 * PI;
    for (float a = startAngle; a < endAngle; a += 0.05f) {
        int16_t x = cx + (int16_t)(r * cosf(a));
        int16_t y = cy + (int16_t)(r * sinf(a));
        canvas.drawPixel(x, y, Theme::ACCENT_GREEN);
        canvas.drawPixel(x + 1, y, Theme::ACCENT_GREEN);
        canvas.drawPixel(x, y + 1, Theme::ACCENT_GREEN);
    }
}
