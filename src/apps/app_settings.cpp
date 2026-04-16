#include "app_settings.h"
#include "app_watchface.h"
#include "../core/screen_manager.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../hal/hal_display.h"
#include "../hal/hal_buzzer.h"
#include "../assets/wallpapers.h"

static const char* itemNames[] = {
    "Brightness",
    "Sound",
    "Vibration",
    "Watch Face",
    "Time Format",
    "Wallpaper",
    "Step Goal",
    "About"
};

void SettingsApp::onCreate() {
    Storage::instance().loadSettings(settings_);
    statusBar.setTitle("Settings");
}

void SettingsApp::onDestroy() {
    Storage::instance().saveSettings(settings_);
    statusBar.clearTitle();
}

void SettingsApp::applyItem(int8_t item) {
    buzzer.click();
    switch (item) {
        case 0:
            settings_.brightness += 20;
            if (settings_.brightness > 100) settings_.brightness = 20;
            display.setBrightness(settings_.brightness);
            break;
        case 1:
            settings_.soundEnabled = !settings_.soundEnabled;
            buzzer.setEnabled(settings_.soundEnabled);
            break;
        case 2:
            settings_.vibrationEnabled = !settings_.vibrationEnabled;
            break;
        case 3:
            settings_.watchFaceIndex = (settings_.watchFaceIndex + 1) % (uint8_t)WatchFaceStyle::STYLE_COUNT;
            break;
        case 4:
            settings_.timeFormat24h = !settings_.timeFormat24h;
            break;
        case 5:
            settings_.wallpaperIndex = (settings_.wallpaperIndex + 1) % Wallpapers::COUNT;
            break;
        case 6:
            settings_.stepGoal += 1000;
            if (settings_.stepGoal > 30000) settings_.stepGoal = 5000;
            break;
        case 7:
            break;
    }
}

void SettingsApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

#if SOLWEAR_HAS_BUTTONS
    if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
        return;
    }
    if (event.touch.gesture == GestureType::SWIPE_DOWN) {
        if (selectedItem_ > 0) selectedItem_--;
        if (selectedItem_ < scrollOffset_) scrollOffset_ = selectedItem_;
        return;
    }
    if (event.touch.gesture == GestureType::SWIPE_UP) {
        if (selectedItem_ < ITEM_COUNT - 1) selectedItem_++;
        if (selectedItem_ >= scrollOffset_ + 6) scrollOffset_ = selectedItem_ - 5;
        return;
    }
    if (event.touch.gesture == GestureType::TAP) {
        applyItem(selectedItem_);
        return;
    }
#else
    if (event.touch.gesture == GestureType::SWIPE_UP) {
        if (scrollOffset_ < ITEM_COUNT - 6) scrollOffset_++;
        return;
    }
    if (event.touch.gesture == GestureType::SWIPE_DOWN) {
        if (scrollOffset_ > 0) scrollOffset_--;
        return;
    }
    if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
        return;
    }
    if (event.touch.gesture == GestureType::TAP) {
        int16_t ty = event.touch.y - APP_AREA_Y;
        int8_t item = ty / ITEM_HEIGHT + scrollOffset_;
        if (item < 0 || item >= ITEM_COUNT) return;
        applyItem(item);
    }
#endif
}

void SettingsApp::update(uint32_t dt) {}

void SettingsApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);
    statusBar.render(canvas);

    for (uint8_t i = 0; i < 7 && (i + scrollOffset_) < ITEM_COUNT; i++) {
        int16_t y = APP_AREA_Y + i * ITEM_HEIGHT;
        if (y + ITEM_HEIGHT > SCREEN_HEIGHT) break;
        renderItem(canvas, i + scrollOffset_, y, (i + scrollOffset_) == selectedItem_);
    }

    // Scroll indicator
    if (ITEM_COUNT > 6) {
        int16_t barH = (6 * SCREEN_HEIGHT) / ITEM_COUNT;
        int16_t barY = APP_AREA_Y + (scrollOffset_ * (SCREEN_HEIGHT - APP_AREA_Y - barH)) / (ITEM_COUNT - 6);
        canvas.fillRect(SCREEN_WIDTH - 3, barY, 3, barH, Theme::ACCENT);
    }
}

void SettingsApp::renderItem(TFT_eSprite& canvas, uint8_t idx, int16_t y, bool selected) {
    uint16_t bg = selected ? Theme::BG_CARD : Theme::BG_PRIMARY;
    canvas.fillRect(0, y, SCREEN_WIDTH, ITEM_HEIGHT - 1, bg);
    if (selected)
        canvas.drawFastHLine(0, y, 3, Theme::ACCENT);
    canvas.drawFastHLine(Theme::PADDING, y + ITEM_HEIGHT - 1, SCREEN_WIDTH - Theme::PADDING * 2, Theme::BG_SECONDARY);

    // Name
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString(itemNames[idx], Theme::PADDING, y + 10, 2);

    // Value on the right
    char valBuf[32];
    uint16_t valColor = Theme::ACCENT;

    switch (idx) {
        case 0:
            snprintf(valBuf, sizeof(valBuf), "%d%%", settings_.brightness);
            break;
        case 1:
            snprintf(valBuf, sizeof(valBuf), settings_.soundEnabled ? "ON" : "OFF");
            valColor = settings_.soundEnabled ? Theme::SUCCESS : Theme::TEXT_MUTED;
            break;
        case 2:
            snprintf(valBuf, sizeof(valBuf), settings_.vibrationEnabled ? "ON" : "OFF");
            valColor = settings_.vibrationEnabled ? Theme::SUCCESS : Theme::TEXT_MUTED;
            break;
        case 3: {
            const char* faces[] = {"Digital", "Analog", "Minimal", "Solana"};
            snprintf(valBuf, sizeof(valBuf), "%s", faces[settings_.watchFaceIndex % (uint8_t)WatchFaceStyle::STYLE_COUNT]);
            break;
        }
        case 4:
            snprintf(valBuf, sizeof(valBuf), settings_.timeFormat24h ? "24H" : "12H");
            break;
        case 5:
            snprintf(valBuf, sizeof(valBuf), "%s", Wallpapers::getName(settings_.wallpaperIndex));
            break;
        case 6:
            snprintf(valBuf, sizeof(valBuf), "%u", settings_.stepGoal);
            break;
        case 7:
            snprintf(valBuf, sizeof(valBuf), "v1.0");
            valColor = Theme::TEXT_SECONDARY;
            break;
    }

    int16_t vw = canvas.textWidth(valBuf, 2);
    canvas.setTextColor(valColor);
    canvas.drawString(valBuf, SCREEN_WIDTH - vw - Theme::PADDING - 4, y + 10, 2);
}
