#include "app_health.h"
#include "../core/screen_manager.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../hal/hal_imu.h"
#include <math.h>

void HealthApp::onCreate() {
    Settings settings;
    if (Storage::instance().loadSettings(settings)) {
        stepGoal_ = settings.stepGoal;
    }
    Storage::instance().loadWeekSteps(weekSteps_);
    statusBar.setTitle("Health");
}

void HealthApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

#if SOLWEAR_HAS_BUTTONS
    if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
    } else if (event.touch.gesture == GestureType::SWIPE_UP || event.touch.gesture == GestureType::TAP) {
        if (view_ < 1) view_++;
    } else if (event.touch.gesture == GestureType::SWIPE_DOWN) {
        if (view_ > 0) view_--;
    }
#else
    if (event.touch.gesture == GestureType::SWIPE_LEFT) {
        if (view_ < 1) view_++;
    } else if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        if (view_ > 0) view_--;
        else ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
    }
#endif
}

void HealthApp::update(uint32_t dt) {}

void HealthApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);
    statusBar.render(canvas);

    if (view_ == 0) renderToday(canvas);
    else renderHistory(canvas);

    // View indicator dots
    for (int i = 0; i < 2; i++) {
        int16_t dotX = SCREEN_WIDTH / 2 - 6 + i * 12;
        if (i == view_) {
            canvas.fillCircle(dotX, SCREEN_HEIGHT - 10, 3, Theme::ACCENT);
        } else {
            canvas.drawCircle(dotX, SCREEN_HEIGHT - 10, 3, Theme::TEXT_MUTED);
        }
    }
}

void HealthApp::renderToday(TFT_eSprite& canvas) {
    uint32_t steps = imu.getSteps();
    float progress = (float)steps / stepGoal_;
    if (progress > 1.0f) progress = 1.0f;

    int16_t cx = SCREEN_WIDTH / 2;
    int16_t cy = 140;

    // Progress ring
    uint16_t ringColor = progress >= 1.0f ? Theme::ACCENT_GREEN : Theme::ACCENT;
    drawProgressRing(canvas, cx, cy, 70, progress, ringColor);

    // Background ring
    drawProgressRing(canvas, cx, cy, 70, 1.0f, Theme::BG_SECONDARY);
    drawProgressRing(canvas, cx, cy, 70, progress, ringColor);

    // Step count in center
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
    Draw::drawCenteredText(canvas, buf, cy - 15, 4, Theme::TEXT_PRIMARY);

    // "steps" label
    Draw::drawCenteredText(canvas, "steps", cy + 15, 2, Theme::TEXT_SECONDARY);

    // Stats below ring
    float calories = steps * 0.04f;
    float distance = steps * 0.0008f;

    snprintf(buf, sizeof(buf), "%.0f cal", calories);
    canvas.setTextColor(Theme::WARNING);
    canvas.drawString(buf, 20, 230, 2);

    snprintf(buf, sizeof(buf), "%.2f km", distance);
    canvas.setTextColor(Theme::INFO);
    int16_t tw = canvas.textWidth(buf, 2);
    canvas.drawString(buf, SCREEN_WIDTH - 20 - tw, 230, 2);

    // Goal text
    snprintf(buf, sizeof(buf), "Goal: %u", stepGoal_);
    Draw::drawCenteredText(canvas, buf, 255, 1, Theme::TEXT_MUTED);
}

void HealthApp::renderHistory(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "This Week", APP_AREA_Y + 8, 2, Theme::TEXT_PRIMARY);

    static const char* days[] = {"M", "T", "W", "T", "F", "S", "S"};
    uint16_t maxSteps = stepGoal_;
    for (int i = 0; i < 7; i++) {
        if (weekSteps_[i] > maxSteps) maxSteps = weekSteps_[i];
    }

    int16_t barAreaY = APP_AREA_Y + 40;
    int16_t barAreaH = 150;
    int16_t barW = 24;
    int16_t spacing = (SCREEN_WIDTH - 7 * barW) / 8;

    for (int i = 0; i < 7; i++) {
        int16_t x = spacing + i * (barW + spacing);
        float h = (maxSteps > 0) ? ((float)weekSteps_[i] / maxSteps) * barAreaH : 0;
        if (h < 2 && weekSteps_[i] > 0) h = 2;

        int16_t barY = barAreaY + barAreaH - (int16_t)h;
        uint16_t color = (weekSteps_[i] >= stepGoal_) ? Theme::ACCENT_GREEN : Theme::BG_CARD;
        canvas.fillRoundRect(x, barY, barW, (int16_t)h, 3, color);

        // Day label
        canvas.setTextColor(Theme::TEXT_SECONDARY);
        int16_t tw = canvas.textWidth(days[i], 1);
        canvas.drawString(days[i], x + barW / 2 - tw / 2, barAreaY + barAreaH + 6, 1);

        // Step count above bar
        if (weekSteps_[i] > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)(weekSteps_[i] / 1000));
            canvas.setTextColor(Theme::TEXT_MUTED);
            int16_t ntw = canvas.textWidth(buf, 1);
            canvas.drawString(buf, x + barW / 2 - ntw / 2, barY - 12, 1);
        }
    }

    // Legend
    Draw::drawCenteredText(canvas, "(thousands)", barAreaY + barAreaH + 22, 1, Theme::TEXT_MUTED);
}

void HealthApp::drawProgressRing(TFT_eSprite& canvas, int16_t cx, int16_t cy,
                                  int16_t r, float progress, uint16_t color) {
    float startAngle = -PI / 2;
    float endAngle = startAngle + progress * 2 * PI;
    float step = 0.02f;

    for (float a = startAngle; a < endAngle; a += step) {
        for (int16_t dr = -2; dr <= 2; dr++) {
            int16_t x = cx + (int16_t)((r + dr) * cosf(a));
            int16_t y = cy + (int16_t)((r + dr) * sinf(a));
            canvas.drawPixel(x, y, color);
        }
    }
}
