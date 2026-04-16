#include "app_stats.h"
#include "../hal/hal_battery.h"
#include "../hal/hal_imu.h"
#include "../hal/hal_thermal.h"
#include "../core/screen_manager.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"

// Approximate average draw with screen on (mA). Used for time-remaining estimate.
static constexpr float AVG_DRAW_MA = 80.0f;
// Battery capacity (mAh) — Waveshare module ships with a small ~250 mAh cell
// but the user mentioned a 1200 mAh pack. Pick a conservative middle.
static constexpr float BATTERY_CAPACITY_MAH = 1200.0f;

static uint32_t getFreeHeapBytes() {
#if defined(ARDUINO_ARCH_RP2040)
    return (uint32_t)rp2040.getFreeHeap();
#elif defined(ARDUINO_ARCH_ESP32)
    return (uint32_t)ESP.getFreeHeap();
#else
    return 0;
#endif
}

void StatsApp::onCreate() {
    statusBar.setTitle("Stats");
    page_ = 0;
    refreshMs_ = 0;
}

void StatsApp::onDestroy() {
    statusBar.clearTitle();
}

void StatsApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

#if SOLWEAR_HAS_BUTTONS
    if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen();
    } else if (event.touch.gesture == GestureType::TAP ||
               event.touch.gesture == GestureType::SWIPE_UP ||
               event.touch.gesture == GestureType::SWIPE_DOWN) {
        page_ = (page_ + 1) % 2;
    }
#else
    if (event.touch.gesture == GestureType::SWIPE_LEFT ||
        event.touch.gesture == GestureType::SWIPE_RIGHT) {
        page_ = (page_ + 1) % 2;
    } else if (event.touch.gesture == GestureType::SWIPE_DOWN ||
               event.touch.gesture == GestureType::SWIPE_UP) {
        ScreenManager::instance().popScreen();
    }
#endif
}

void StatsApp::update(uint32_t dt) {
    refreshMs_ += dt;
    if (refreshMs_ >= 500) {
        refreshMs_ = 0;
        // Force fresh samples so the user sees live numbers in the stats UI.
        battery.update();
        thermal.update();
    }
}

void StatsApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);
    statusBar.render(canvas);

    if (page_ == 0) renderBatteryPage(canvas);
    else            renderSystemPage(canvas);

    // Page indicator dots
    int16_t cy = SCREEN_HEIGHT - 14;
    for (int i = 0; i < 2; i++) {
        uint16_t c = (i == page_) ? Theme::ACCENT : Theme::BG_CARD;
        canvas.fillCircle(SCREEN_WIDTH / 2 - 8 + i * 16, cy, 3, c);
    }
}

void StatsApp::renderBatteryPage(TFT_eSprite& canvas) {
    int16_t y = APP_AREA_Y + 8;

    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(Theme::ACCENT);
    canvas.drawString("Battery", SCREEN_WIDTH / 2, y + 8, 4);
    y += 36;

    // Big percent
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", battery.getPercent());
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString(buf, SCREEN_WIDTH / 2, y + 16, 7);
    y += 56;

    canvas.setTextDatum(TL_DATUM);

    // Voltage
    snprintf(buf, sizeof(buf), "Voltage: %.3f V", battery.getVoltage());
    canvas.setTextColor(Theme::TEXT_SECONDARY);
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 22;

    // Charging status
    canvas.setTextColor(battery.isCharging() ? Theme::ACCENT_GREEN : Theme::TEXT_MUTED);
    canvas.drawString(battery.isCharging() ? "Status: Charging" : "Status: Discharging",
                      Theme::PADDING, y, 2);
    y += 22;

    // Estimated time remaining (rough)
    float capRemainingMah = BATTERY_CAPACITY_MAH * (battery.getPercent() / 100.0f);
    float hours = capRemainingMah / AVG_DRAW_MA;
    int hh = (int)hours;
    int mm = (int)((hours - hh) * 60);
    snprintf(buf, sizeof(buf), "Est: %dh %02dm", hh, mm);
    canvas.setTextColor(Theme::TEXT_SECONDARY);
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 22;

    // Critical/low warning
    if (battery.isCritical()) {
        canvas.setTextColor(Theme::DANGER);
        canvas.drawString("CRITICAL", Theme::PADDING, y, 2);
    } else if (battery.isLow()) {
        canvas.setTextColor(Theme::WARNING);
        canvas.drawString("LOW", Theme::PADDING, y, 2);
    }
}

void StatsApp::renderSystemPage(TFT_eSprite& canvas) {
    int16_t y = APP_AREA_Y + 8;

    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(Theme::ACCENT);
    canvas.drawString("System", SCREEN_WIDTH / 2, y + 8, 4);
    y += 38;

    canvas.setTextDatum(TL_DATUM);

    char buf[40];

    // Free heap
    uint32_t heap = getFreeHeapBytes();
    snprintf(buf, sizeof(buf), "Heap: %lu B", (unsigned long)heap);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 20;

    // Heap bar (out of total ~256 KB)
    int16_t barW = SCREEN_WIDTH - Theme::PADDING * 2;
    int16_t fill = (int16_t)((heap / 264192.0f) * barW);
    if (fill > barW) fill = barW;
    canvas.drawRect(Theme::PADDING, y, barW, 8, Theme::BG_CARD);
    canvas.fillRect(Theme::PADDING + 1, y + 1, fill - 2, 6, Theme::ACCENT_GREEN);
    y += 16;

    // Uptime
    uint32_t s = millis() / 1000;
    uint32_t hh = s / 3600;
    uint32_t mm = (s % 3600) / 60;
    uint32_t ss = s % 60;
    snprintf(buf, sizeof(buf), "Uptime: %lu:%02lu:%02lu",
             (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    canvas.setTextColor(Theme::TEXT_SECONDARY);
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 20;

    // CPU freq
    snprintf(buf, sizeof(buf), "CPU: %lu MHz", (unsigned long)(F_CPU / 1000000));
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 20;

    // Steps today
    canvas.setTextColor(Theme::TEXT_SECONDARY);
    snprintf(buf, sizeof(buf), "Steps: %lu", (unsigned long)imu.getSteps());
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 20;

    // Temperature (RP2040 internal sensor) — color coded
    float t = thermal.getTemperatureC();
    uint16_t tempColor = Theme::ACCENT_GREEN;
    if (t > 65.0f)      tempColor = Theme::DANGER;
    else if (t > 50.0f) tempColor = 0xFC00; // orange
    else if (t > 40.0f) tempColor = Theme::WARNING;
    canvas.setTextColor(tempColor);
    snprintf(buf, sizeof(buf), "Temp: %.1f C", t);
    canvas.drawString(buf, Theme::PADDING, y, 2);
    y += 20;

    // Firmware version
    canvas.setTextColor(Theme::TEXT_MUTED);
    canvas.drawString("FW: SolWearOS v1.0", Theme::PADDING, y, 2);
}
