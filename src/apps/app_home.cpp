#include "app_home.h"
#include "../core/screen_manager.h"
#include "../core/storage.h"
#include "../assets/icons.h"
#include "../assets/wallpapers.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../hal/hal_buzzer.h"

// App grid starts below status bar area
#define GRID_START_Y    32
#define GRID_CELL_W     60
#define GRID_CELL_H     74

void HomeApp::onCreate() {
    Settings settings;
    if (Storage::instance().loadSettings(settings)) {
        wallpaperIdx_ = settings.wallpaperIndex;
    }
}

void HomeApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

#if SOLWEAR_HAS_BUTTONS
    auto& registry = AppRegistry::instance();
    uint8_t totalApps = 0;
    for (uint8_t i = 0; i < registry.getCount(); i++) {
        AppId id = registry.getEntries()[i].id;
        if (id != APP_WATCHFACE && id != APP_HOME && id != APP_CHARGING) totalApps++;
    }
    if (highlightIdx_ < 0) highlightIdx_ = 0;

    if (event.touch.gesture == GestureType::SWIPE_DOWN) {
        highlightIdx_ = (highlightIdx_ <= 0) ? totalApps - 1 : highlightIdx_ - 1;
        return;
    } else if (event.touch.gesture == GestureType::SWIPE_UP) {
        highlightIdx_ = (highlightIdx_ + 1) % totalApps;
        return;
    } else if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen(Transition::SLIDE_DOWN);
        return;
    } else if (event.touch.gesture == GestureType::TAP) {
        uint8_t gridIdx = 0;
        for (uint8_t i = 0; i < registry.getCount(); i++) {
            const AppEntry* entry = &registry.getEntries()[i];
            if (entry->id == APP_WATCHFACE || entry->id == APP_HOME || entry->id == APP_CHARGING) continue;
            if (gridIdx == highlightIdx_) {
                highlightTime_ = millis();
                buzzer.click();
                ScreenManager::instance().pushScreen(entry->id, Transition::SLIDE_LEFT);
                return;
            }
            gridIdx++;
        }
        return;
    }
#else
    if (event.touch.gesture == GestureType::SWIPE_DOWN) {
        ScreenManager::instance().popScreen(Transition::SLIDE_DOWN);
        return;
    }

    if (event.touch.gesture == GestureType::TAP) {
        int16_t tx = event.touch.x;
        int16_t ty = event.touch.y;

        // Determine which icon was tapped
        auto& registry = AppRegistry::instance();
        uint8_t count = registry.getCount();

        for (uint8_t i = 0; i < count; i++) {
            const AppEntry* entry = &registry.getEntries()[i];
            // Skip watchface, home, charging — they're not launchable from grid
            if (entry->id == APP_WATCHFACE || entry->id == APP_HOME ||
                entry->id == APP_CHARGING) continue;

            // Calculate grid position (skip first two entries: watchface, home)
            uint8_t gridIdx = 0;
            for (uint8_t j = 0; j < i; j++) {
                AppId jid = registry.getEntries()[j].id;
                if (jid != APP_WATCHFACE && jid != APP_HOME && jid != APP_CHARGING) {
                    gridIdx++;
                }
            }

            uint8_t col = gridIdx % ICON_GRID_COLS;
            uint8_t row = gridIdx / ICON_GRID_COLS;
            int16_t ix = col * GRID_CELL_W + (GRID_CELL_W - ICON_SIZE) / 2;
            int16_t iy = GRID_START_Y + row * GRID_CELL_H;

            Rect iconRect = {ix, iy, ICON_SIZE, ICON_SIZE};
            if (iconRect.contains(tx, ty)) {
                highlightIdx_ = gridIdx;
                highlightTime_ = millis();
                buzzer.click();
                // Launch app after brief highlight
                ScreenManager::instance().pushScreen(entry->id, Transition::SLIDE_LEFT);
                return;
            }
        }
    }
#endif
}

void HomeApp::update(uint32_t dt) {
    // Clear highlight after 200ms
    if (highlightIdx_ >= 0 && millis() - highlightTime_ > 200) {
        highlightIdx_ = -1;
    }
}

void HomeApp::render(TFT_eSprite& canvas) {
    // Wallpaper background
    Wallpapers::draw(canvas, wallpaperIdx_);

    // Dark overlay for readability
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x += 2) {
            canvas.drawPixel(x + (y % 2), y, TFT_BLACK);
        }
    }

    // Status bar
    statusBar.render(canvas);

    // Draw app icons in grid
    auto& registry = AppRegistry::instance();
    uint8_t gridIdx = 0;

    for (uint8_t i = 0; i < registry.getCount(); i++) {
        const AppEntry* entry = &registry.getEntries()[i];
        if (entry->id == APP_WATCHFACE || entry->id == APP_HOME ||
            entry->id == APP_CHARGING) continue;

        uint8_t col = gridIdx % ICON_GRID_COLS;
        uint8_t row = gridIdx / ICON_GRID_COLS;
        int16_t ix = col * GRID_CELL_W + (GRID_CELL_W - ICON_SIZE) / 2;
        int16_t iy = GRID_START_Y + row * GRID_CELL_H;

        bool highlight = (gridIdx == highlightIdx_);
        drawIcon(canvas, i, ix, iy, highlight);

        // App name below icon
        int16_t nameX = col * GRID_CELL_W + GRID_CELL_W / 2;
        int16_t nameY = iy + ICON_SIZE + 2;
        canvas.setTextColor(Theme::TEXT_PRIMARY);
        int16_t tw = canvas.textWidth(entry->name, 1);
        canvas.drawString(entry->name, nameX - tw / 2, nameY, 1);

        gridIdx++;
    }
}

void HomeApp::drawIcon(TFT_eSprite& canvas, uint8_t regIndex, int16_t x, int16_t y, bool highlight) {
    const AppEntry* entry = &AppRegistry::instance().getEntries()[regIndex];

    // Draw procedural icon based on app ID
    switch (entry->id) {
        case APP_SETTINGS: Icons::drawSettings(canvas, x, y); break;
        case APP_WALLET:   Icons::drawWallet(canvas, x, y); break;
        case APP_NFC:      Icons::drawNfc(canvas, x, y); break;
        case APP_HEALTH:   Icons::drawHealth(canvas, x, y); break;
        case APP_GAME:     Icons::drawGame(canvas, x, y); break;
        case APP_ALARM:    Icons::drawAlarm(canvas, x, y); break;
        case APP_STATS:    Icons::drawStats(canvas, x, y); break;
        default:
            canvas.fillRoundRect(x, y, ICON_SIZE, ICON_SIZE, 10, Theme::BG_CARD);
            break;
    }

    // Highlight overlay
    if (highlight) {
        canvas.drawRoundRect(x - 1, y - 1, ICON_SIZE + 2, ICON_SIZE + 2, 10, Theme::ACCENT);
        canvas.drawRoundRect(x - 2, y - 2, ICON_SIZE + 4, ICON_SIZE + 4, 11, Theme::ACCENT);
    }
}
