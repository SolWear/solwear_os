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
        style_ = (WatchFaceStyle)(settings.watchFaceIndex % (uint8_t)WatchFaceStyle::STYLE_COUNT);
        use24Hour_ = settings.timeFormat24h;
        wallpaperIdx_ = settings.wallpaperIndex;
    }
}

void WatchFaceApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

    switch (event.touch.gesture) {
        case GestureType::SWIPE_UP:
            ScreenManager::instance().pushScreen(APP_HOME, Transition::SLIDE_UP);
            break;
        case GestureType::TAP:
            style_ = (WatchFaceStyle)(((uint8_t)style_ + 1) % (uint8_t)WatchFaceStyle::STYLE_COUNT);
            break;
#if SOLWEAR_HAS_BUTTONS
        case GestureType::SWIPE_DOWN:
            // K1: cycle face backwards
            style_ = (WatchFaceStyle)(((uint8_t)style_ + (uint8_t)WatchFaceStyle::STYLE_COUNT - 1) % (uint8_t)WatchFaceStyle::STYLE_COUNT);
            break;
#endif
        default:
            break;
    }
}

void WatchFaceApp::update(uint32_t dt) {
    settingsPollMs_ += dt;
    if (settingsPollMs_ >= 3000U) {
        settingsPollMs_ = 0;
        Settings settings;
        if (Storage::instance().loadSettings(settings)) {
            use24Hour_ = settings.timeFormat24h;
        }
    }
}

void WatchFaceApp::formatClock(char* buf, size_t len, bool withSeconds) const {
    DateTime dt = SystemClock::instance().now();
    uint8_t hour = dt.hour;
    if (!use24Hour_) {
        hour = (uint8_t)(hour % 12);
        if (hour == 0) hour = 12;
    }

    if (withSeconds) {
        snprintf(buf, len, "%02u:%02u:%02u", hour, dt.minute, dt.second);
    } else {
        snprintf(buf, len, "%02u:%02u", hour, dt.minute);
    }
}

void WatchFaceApp::render(TFT_eSprite& canvas) {
    // Draw wallpaper
    Wallpapers::draw(canvas, wallpaperIdx_);

    switch (style_) {
        case WatchFaceStyle::DIGITAL:  renderDigital(canvas);  break;
        case WatchFaceStyle::ANALOG_FACE: renderAnalog(canvas); break;
        case WatchFaceStyle::MINIMAL:  renderMinimal(canvas);  break;
        case WatchFaceStyle::SOLANA:   renderSolana(canvas);   break;
    }

    // Battery indicator (top right)
    Draw::drawBatteryIcon(canvas, SCREEN_WIDTH - 30, 6, battery.getPercent(), battery.isCharging());
}

void WatchFaceApp::renderDigital(TFT_eSprite& canvas) {
    char buf[16];

    // Large time
    formatClock(buf, sizeof(buf), false);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    Draw::drawCenteredText(canvas, buf, 90, 7, Theme::TEXT_PRIMARY);

    // Seconds
    DateTime dt = SystemClock::instance().now();
    snprintf(buf, sizeof(buf), "%02d", dt.second);
    Draw::drawCenteredText(canvas, buf, 145, 4, Theme::ACCENT);

    // Date
    SystemClock::instance().formatDate(buf, sizeof(buf));
    Draw::drawCenteredText(canvas, buf, 175, 2, Theme::TEXT_SECONDARY);

    if (!use24Hour_) {
        Draw::drawCenteredText(canvas, dt.hour >= 12 ? "PM" : "AM", 205, 2, Theme::TEXT_MUTED);
    }

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
    DateTime dt = SystemClock::instance().now();

    // Time only, large centered
    char buf[6];
    formatClock(buf, sizeof(buf), false);
    Draw::drawCenteredText(canvas, buf, 115, 7, Theme::TEXT_PRIMARY);

    if (!use24Hour_) {
        Draw::drawCenteredText(canvas, dt.hour >= 12 ? "PM" : "AM", 160, 2, Theme::TEXT_MUTED);
    }

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

void WatchFaceApp::renderSolana(TFT_eSprite& canvas) {
    DateTime dt = SystemClock::instance().now();
    char buf[24];

    auto drawSolanaBar = [&canvas](int16_t x, int16_t y, int16_t w, int16_t h, int16_t slant, uint16_t color) {
        canvas.fillRect(x + slant, y, w - slant * 2, h, color);
        canvas.fillTriangle(x, y + h - 1, x + slant, y, x + slant, y + h - 1, color);
        canvas.fillTriangle(x + w - slant, y, x + w, y, x + w - slant, y + h - 1, color);
    };

    formatClock(buf, sizeof(buf), false);
    Draw::drawCenteredText(canvas, buf, 40, 7, Theme::TEXT_PRIMARY);

    const uint8_t gmFont = 5;
    const int16_t gmY = 76;
    const int16_t gmX = (SCREEN_WIDTH - canvas.textWidth("GM", gmFont)) / 2;
    canvas.setTextColor(Theme::ACCENT_SOL);
    canvas.drawString("GM", gmX - 1, gmY, gmFont);
    canvas.drawString("GM", gmX + 1, gmY, gmFont);
    canvas.drawString("GM", gmX, gmY - 1, gmFont);
    canvas.drawString("GM", gmX, gmY + 1, gmFont);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString("GM", gmX, gmY, gmFont);

    drawSolanaBar(48, 96, 144, 20, 16, Theme::ACCENT_SOL);
    drawSolanaBar(48, 132, 144, 20, 16, Theme::ACCENT);
    drawSolanaBar(48, 168, 144, 20, 16, Theme::ACCENT_GREEN);

    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    Draw::drawCenteredText(canvas, buf, 216, 2, Theme::TEXT_SECONDARY);

    if (!use24Hour_) {
        Draw::drawCenteredText(canvas, dt.hour >= 12 ? "PM" : "AM", 194, 2, Theme::TEXT_MUTED);
    }

    uint32_t steps = imu.getSteps();
    snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)steps);
    Draw::drawCenteredText(canvas, buf, 238, 2, Theme::ACCENT);
}
