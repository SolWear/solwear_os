#include "app_charging.h"
#include "../hal/hal_battery.h"
#include "../hal/hal_display.h"
#include "../core/screen_manager.h"
#include "../ui/ui_common.h"
#include <math.h>

void ChargingApp::onCreate() {
    animMs_ = 0;
    fillPhase_ = 0.0f;
    lastPercent_ = battery.getPercent();
    displayVolts_ = battery.getVoltage();
}

void ChargingApp::onDestroy() {
    // Nothing to clean up; battery HAL is global.
}

void ChargingApp::onEvent(const Event& event) {
    // Charging screen is non-dismissable; auto-pops in update() when USB removed.
    (void)event;
}

void ChargingApp::update(uint32_t dt) {
    animMs_ += dt;
    fillPhase_ += dt / 1500.0f;  // ~1.5s per fill cycle
    if (fillPhase_ > 1.0f) fillPhase_ -= 1.0f;

    lastPercent_ = battery.getPercent();
    displayVolts_ = battery.getVoltage();

    // Auto-dismiss when charger removed.
    if (!battery.isCharging()) {
        ScreenManager::instance().popScreen(Transition::SLIDE_DOWN);
    }
}

void ChargingApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);

    const int16_t cx = SCREEN_WIDTH / 2;

    // --- Big battery shape ---
    const int16_t batX = cx - 50;
    const int16_t batY = 60;
    const int16_t batW = 100;
    const int16_t batH = 160;
    const int16_t tipW = 26;
    const int16_t tipH = 8;

    // Battery tip
    canvas.fillRoundRect(cx - tipW / 2, batY - tipH, tipW, tipH + 2, 2, Theme::TEXT_PRIMARY);
    // Outer body
    canvas.drawRoundRect(batX, batY, batW, batH, 8, Theme::TEXT_PRIMARY);
    canvas.drawRoundRect(batX + 1, batY + 1, batW - 2, batH - 2, 7, Theme::TEXT_PRIMARY);

    // Animated fill height: grows from current % to 100% using sine wave
    float pctNorm = lastPercent_ / 100.0f;
    float swing  = 0.5f - 0.5f * cosf(fillPhase_ * 2.0f * 3.14159265f);
    float visiblePct = pctNorm + (1.0f - pctNorm) * swing;
    if (visiblePct > 1.0f) visiblePct = 1.0f;

    int16_t fillH = (int16_t)((batH - 8) * visiblePct);
    int16_t fillY = batY + batH - 4 - fillH;

    // Color by battery level
    uint16_t fillColor = Theme::ACCENT_GREEN;
    if (lastPercent_ < 20) fillColor = Theme::WARNING;

    canvas.fillRoundRect(batX + 4, fillY, batW - 8, fillH, 4, fillColor);

    // --- Lightning bolt overlay ---
    // Procedural polygon (zig-zag), centered on battery
    const int16_t bx = cx;
    const int16_t by = batY + batH / 2;
    canvas.fillTriangle(bx - 10, by - 30, bx + 8, by - 4, bx - 2, by - 4, TFT_WHITE);
    canvas.fillTriangle(bx + 8, by - 4, bx - 2, by - 4, bx + 12, by + 6, TFT_WHITE);
    canvas.fillTriangle(bx + 12, by + 6, bx - 2, by - 4, bx - 12, by + 30, TFT_WHITE);

    // --- Percent text below the battery ---
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", lastPercent_);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(pctStr, cx, batY + batH + 18, 4);

    // --- Voltage text ---
    char vStr[12];
    snprintf(vStr, sizeof(vStr), "%.2f V", displayVolts_);
    canvas.setTextColor(Theme::ACCENT);
    canvas.drawString(vStr, cx, batY + batH + 42, 2);

    // --- "Charging" label up top ---
    canvas.setTextColor(Theme::ACCENT_GREEN);
    canvas.drawString("Charging", cx, 30, 2);

    canvas.setTextDatum(TL_DATUM);  // restore default
}
