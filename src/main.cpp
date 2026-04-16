// ============================================================
// SolWearOS — Smartwatch OS for RP2040
// Main Entry Point
//
// Single-core (core 0) — UI rendering, touch, IMU, battery, events @ 30fps.
// FreeRTOS in earlephilhower v5.x conflicts with setup1/loop1 stacks.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "config.h"

// HAL
#include "hal/hal_display.h"
#include "hal/hal_touch.h"
#include "hal/hal_imu.h"
#include "hal/hal_nfc.h"
#include "hal/hal_buzzer.h"
#include "hal/hal_battery.h"
#include "hal/hal_power.h"
#include "hal/hal_thermal.h"
#if SOLWEAR_EINK_TARGET
#include "hal/hal_eink.h"
#endif

// Core OS
#include "core/event_system.h"
#include "core/screen_manager.h"
#include "core/app_framework.h"
#include "core/storage.h"
#include "core/system_clock.h"
#include "core/timer_manager.h"

// UI
#include "ui/status_bar.h"

// Apps
#include "apps/app_watchface.h"
#include "apps/app_home.h"
#include "apps/app_settings.h"
#include "apps/app_wallet.h"
#include "apps/app_nfc.h"
#include "apps/app_health.h"
#include "apps/app_game.h"
#include "apps/app_alarm.h"
#include "apps/app_charging.h"
#include "apps/app_stats.h"

// Assets
#include "assets/sounds.h"

// ============================================================
// Global State
// ============================================================
static Settings g_settings;
static WatchFaceApp* g_watchface = nullptr;
static uint32_t g_lastFrameTime = 0;
static uint32_t g_imuTimer = 0;
static uint32_t g_batteryTimer = 0;
static uint32_t g_statusTimer = 0;
static uint32_t g_thermalTimer = 0;
static uint32_t g_powerBtnHeldMs = 0;
static bool g_powerKeyEnabled = false;
static bool g_powerKeySharesLatchPin = false;
static bool g_chargingScreenActive = false;
static char g_serialCmdBuf[64];
static uint8_t g_serialCmdLen = 0;
static uint8_t g_bootStage = 0;
#if SOLWEAR_EINK_TARGET
static AppId g_einkCurrentApp = APP_HOME;
static uint8_t g_einkWatchfaceStyle = 0;
static uint8_t g_lastEinkRenderedMinute = 255;
static uint8_t g_lastEinkRenderedHour = 255;
#endif

static uint32_t getFreeHeapBytes() {
#if defined(ARDUINO_ARCH_RP2040)
    return (uint32_t)rp2040.getFreeHeap();
#elif defined(ARDUINO_ARCH_ESP32)
    return (uint32_t)ESP.getFreeHeap();
#else
    return (uint32_t)0;
#endif
}

static bool parseAppIdToken(const char* token, AppId& outId) {
    if (strcmp(token, "watchface") == 0 || strcmp(token, "watch") == 0 || strcmp(token, "clock") == 0) {
        outId = APP_WATCHFACE;
        return true;
    }
    if (strcmp(token, "home") == 0) {
        outId = APP_HOME;
        return true;
    }
    if (strcmp(token, "settings") == 0) {
        outId = APP_SETTINGS;
        return true;
    }
    if (strcmp(token, "wallet") == 0) {
        outId = APP_WALLET;
        return true;
    }
    if (strcmp(token, "nfc") == 0) {
        outId = APP_NFC;
        return true;
    }
    if (strcmp(token, "health") == 0) {
        outId = APP_HEALTH;
        return true;
    }
    if (strcmp(token, "game") == 0 || strcmp(token, "snake") == 0) {
        outId = APP_GAME;
        return true;
    }
    if (strcmp(token, "alarm") == 0) {
        outId = APP_ALARM;
        return true;
    }
    if (strcmp(token, "charging") == 0) {
        outId = APP_CHARGING;
        return true;
    }
    if (strcmp(token, "stats") == 0) {
        outId = APP_STATS;
        return true;
    }
    return false;
}

#if SOLWEAR_EINK_TARGET
static const uint8_t kSegMap[10] = {
    0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
    0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
};

static constexpr int16_t kHomeCardW = 80;
static constexpr int16_t kHomeCardH = 74;
static constexpr int16_t kHomeGap = 8;
static constexpr int16_t kHomeX0 = 16;
static constexpr int16_t kHomeY0 = 34;

static void einkDrawSegDigit(int16_t x, int16_t y, int16_t s, int digit) {
    if (digit < 0 || digit > 9) return;
    const uint8_t m = kSegMap[digit];
    const int16_t w = s;
    const int16_t h = s * 2;
    const int16_t t = (s < 8) ? 2 : 3;

    // Segments: a,b,c,d,e,f,g
    if (m & 0b1000000) eink.fillRect(x + t, y, w - 2 * t, t, true);
    if (m & 0b0100000) eink.fillRect(x + w - t, y + t, t, h / 2 - t, true);
    if (m & 0b0010000) eink.fillRect(x + w - t, y + h / 2, t, h / 2 - t, true);
    if (m & 0b0001000) eink.fillRect(x + t, y + h - t, w - 2 * t, t, true);
    if (m & 0b0000100) eink.fillRect(x, y + h / 2, t, h / 2 - t, true);
    if (m & 0b0000010) eink.fillRect(x, y + t, t, h / 2 - t, true);
    if (m & 0b0000001) eink.fillRect(x + t, y + h / 2 - t / 2, w - 2 * t, t, true);
}

static void einkDrawColon(int16_t x, int16_t y, int16_t s) {
    const int16_t r = (s < 8) ? 2 : 3;
    eink.fillRect(x, y + s / 2, r, r, true);
    eink.fillRect(x, y + s + r, r, r, true);
}

static uint8_t einkDisplayHour(const DateTime& dt, bool use24Hour) {
    if (use24Hour) {
        return dt.hour;
    }
    uint8_t h = (uint8_t)(dt.hour % 12);
    return h == 0 ? 12 : h;
}

static void einkDrawTime(int16_t x, int16_t y, int16_t s, const DateTime& dt, bool use24Hour) {
    const uint8_t hour = einkDisplayHour(dt, use24Hour);
    const int16_t step = s + 6;
    einkDrawSegDigit(x, y, s, hour / 10);
    einkDrawSegDigit(x + step, y, s, hour % 10);
    einkDrawColon(x + step * 2 - 2, y, s);
    einkDrawSegDigit(x + step * 2 + 4, y, s, dt.minute / 10);
    einkDrawSegDigit(x + step * 3 + 4, y, s, dt.minute % 10);
}

static void einkDrawIconWallet(int16_t x, int16_t y, int16_t s) {
    eink.drawRect(x, y + s / 4, s, s / 2, true);
    eink.drawRect(x + s / 5, y + s / 3, (s * 2) / 5, s / 5, true);
    eink.fillRect(x + (s * 3) / 5, y + s / 2 - 2, 4, 4, true);
}

static void einkDrawIconHeart(int16_t x, int16_t y, int16_t s) {
    const int16_t cx = x + s / 2;
    const int16_t cy = y + s / 2;
    eink.fillRect(cx - s / 4, cy - s / 4, s / 6, s / 6, true);
    eink.fillRect(cx + s / 12, cy - s / 4, s / 6, s / 6, true);
    for (int16_t i = 0; i < s / 3; ++i) {
        eink.drawLine(cx - s / 3 + i, cy - s / 8 + i, cx + s / 3 - i, cy - s / 8 + i, true);
    }
}

static void einkDrawIconLock(int16_t x, int16_t y, int16_t s) {
    eink.drawRect(x + s / 5, y + s / 3, (s * 3) / 5, s / 2, true);
    eink.drawLine(x + s / 3, y + s / 3, x + s / 3, y + s / 5, true);
    eink.drawLine(x + (s * 2) / 3, y + s / 3, x + (s * 2) / 3, y + s / 5, true);
    eink.drawLine(x + s / 3, y + s / 5, x + (s * 2) / 3, y + s / 5, true);
    eink.fillRect(x + s / 2 - 2, y + s / 2, 4, s / 8, true);
}

static void einkDrawIconGear(int16_t x, int16_t y, int16_t s) {
    const int16_t ox = x + s / 4;
    const int16_t oy = y + s / 4;
    const int16_t w = s / 2;
    eink.drawRect(ox, oy, w, w, true);
    eink.fillRect(ox + w / 3, oy + w / 3, w / 3, w / 3, true);
    eink.fillRect(ox + w / 2 - 2, oy - 4, 4, 4, true);
    eink.fillRect(ox + w / 2 - 2, oy + w, 4, 4, true);
    eink.fillRect(ox - 4, oy + w / 2 - 2, 4, 4, true);
    eink.fillRect(ox + w, oy + w / 2 - 2, 4, 4, true);
}

static void einkDrawCardShell(int16_t x, int16_t y) {
    eink.drawRect(x, y, kHomeCardW, kHomeCardH, true);
    eink.fillRect(x + 4, y + 4, 10, 2, true);
    eink.fillRect(x + kHomeCardW - 14, y + 4, 10, 2, true);
    eink.fillRect(x + 4, y + kHomeCardH - 6, 10, 2, true);
    eink.fillRect(x + kHomeCardW - 14, y + kHomeCardH - 6, 10, 2, true);
}

static bool einkPointInRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

static bool einkMapTapToHomeApp(int16_t x, int16_t y, AppId& outId) {
    const int16_t x1 = kHomeX0 + kHomeCardW + kHomeGap;
    const int16_t y1 = kHomeY0 + kHomeCardH + kHomeGap;

    if (einkPointInRect(x, y, kHomeX0, kHomeY0, kHomeCardW, kHomeCardH)) {
        outId = APP_WALLET;
        return true;
    }
    if (einkPointInRect(x, y, x1, kHomeY0, kHomeCardW, kHomeCardH)) {
#if SOLWEAR_HAS_IMU
        outId = APP_HEALTH;
#else
        outId = APP_STATS;
#endif
        return true;
    }
    if (einkPointInRect(x, y, kHomeX0, y1, kHomeCardW, kHomeCardH)) {
#if SOLWEAR_HAS_NFC
        outId = APP_NFC;
#else
        outId = APP_ALARM;
#endif
        return true;
    }
    if (einkPointInRect(x, y, x1, y1, kHomeCardW, kHomeCardH)) {
        outId = APP_SETTINGS;
        return true;
    }
    return false;
}

static void einkDrawMainGrid() {
    const int16_t x1 = kHomeX0 + kHomeCardW + kHomeGap;
    const int16_t y1 = kHomeY0 + kHomeCardH + kHomeGap;

    einkDrawCardShell(kHomeX0, kHomeY0);
    einkDrawCardShell(x1, kHomeY0);
    einkDrawCardShell(kHomeX0, y1);
    einkDrawCardShell(x1, y1);

    einkDrawIconWallet(30, 52, 38);
    einkDrawIconHeart(118, 52, 38);
    einkDrawIconLock(30, 134, 38);
    einkDrawIconGear(118, 134, 38);
}

static void einkDrawTopClockBar(const DateTime& dt) {
    eink.drawRect(2, 2, 196, 24, true);
    eink.drawRect(8, 7, 14, 12, true);
    eink.fillRect(22, 10, 2, 6, true);
    eink.fillRect(176, 8, 12, 2, true);
    eink.fillRect(176, 12, 12, 2, true);
    eink.fillRect(176, 16, 12, 2, true);
    einkDrawTime(58, 6, 12, dt, g_settings.timeFormat24h);
    if (!g_settings.timeFormat24h) {
        if (dt.hour >= 12) {
            eink.fillRect(164, 7, 8, 8, true);
        } else {
            eink.drawRect(164, 7, 8, 8, true);
        }
    }
}

static void einkDrawWatchfaceDigital(const DateTime& dt) {
    einkDrawTopClockBar(dt);
    eink.drawRect(12, 36, 176, 152, true);
    einkDrawTime(34, 80, 22, dt, g_settings.timeFormat24h);
}

static void einkDrawWatchfaceAnalog(const DateTime& dt) {
    einkDrawTopClockBar(dt);
    const int16_t cx = 100;
    const int16_t cy = 112;
    const int16_t r = 62;

    eink.drawCircle(cx, cy, r, true);
    for (int i = 0; i < 12; ++i) {
        float a = (float)i * 30.0f * PI / 180.0f - PI / 2.0f;
        int16_t x1 = cx + (int16_t)((r - 6) * cosf(a));
        int16_t y1 = cy + (int16_t)((r - 6) * sinf(a));
        int16_t x2 = cx + (int16_t)(r * cosf(a));
        int16_t y2 = cy + (int16_t)(r * sinf(a));
        eink.drawLine(x1, y1, x2, y2, true);
    }

    float ha = ((dt.hour % 12) + dt.minute / 60.0f) * 30.0f * PI / 180.0f - PI / 2.0f;
    float ma = dt.minute * 6.0f * PI / 180.0f - PI / 2.0f;
    int16_t hx = cx + (int16_t)(30 * cosf(ha));
    int16_t hy = cy + (int16_t)(30 * sinf(ha));
    int16_t mx = cx + (int16_t)(46 * cosf(ma));
    int16_t my = cy + (int16_t)(46 * sinf(ma));
    eink.drawLine(cx, cy, hx, hy, true);
    eink.drawLine(cx, cy, mx, my, true);
    eink.fillRect(cx - 2, cy - 2, 4, 4, true);
}

static void einkDrawWatchfaceMinimal(const DateTime& dt) {
    einkDrawTopClockBar(dt);
    eink.drawRect(12, 36, 176, 152, true);
    einkDrawTime(42, 95, 18, dt, g_settings.timeFormat24h);

    const int16_t secBar = (dt.second * 164) / 59;
    eink.drawRect(18, 172, 164, 10, true);
    eink.fillRect(20, 174, secBar, 6, true);
}

static void einkDrawSolanaBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t skew) {
    for (int16_t row = 0; row < h; ++row) {
        int16_t dx = (row * skew) / h;
        eink.fillRect(x + dx, y + row, w, 1, true);
    }
}

static void einkDrawGlyphG(int16_t x, int16_t y, int16_t w, int16_t h) {
    const int16_t t = 5;
    const int16_t midY = y + h / 2 - t / 2;

    eink.fillRect(x, y, w, t, true);
    eink.fillRect(x, y, t, h, true);
    eink.fillRect(x, y + h - t, w, t, true);
    eink.fillRect(x + w - t, midY, t, h - (h / 2 - t / 2), true);
    eink.fillRect(x + w / 2 - 1, midY, w / 2 + 1, t, true);
}

static void einkDrawGlyphM(int16_t x, int16_t y, int16_t w, int16_t h) {
    const int16_t t = 5;
    eink.fillRect(x, y, t, h, true);
    eink.fillRect(x + w - t, y, t, h, true);
    for (int16_t i = 0; i < t; ++i) {
        eink.drawLine(x + t + i, y, x + w / 2, y + h / 2 + 2, true);
        eink.drawLine(x + w - t - 1 - i, y, x + w / 2, y + h / 2 + 2, true);
    }
}

static void einkDrawGmMonogram(int16_t x, int16_t y) {
    const int16_t glyphW = 34;
    const int16_t glyphH = 26;
    einkDrawGlyphG(x, y, glyphW, glyphH);
    einkDrawGlyphM(x + glyphW + 10, y, glyphW, glyphH);
}

static void einkDrawWatchfaceSolana(const DateTime& dt) {
    einkDrawTopClockBar(dt);
    eink.drawRect(12, 36, 176, 152, true);

    einkDrawGmMonogram(61, 40);

    // Stylized Solana mark in monochrome e-ink.
    einkDrawSolanaBar(48, 68, 92, 14, 10);
    einkDrawSolanaBar(62, 100, 92, 14, -10);
    einkDrawSolanaBar(48, 132, 92, 14, 10);

    einkDrawTime(34, 154, 14, dt, g_settings.timeFormat24h);
}

static void einkDrawAppFrame(AppId id, const DateTime& dt) {
    einkDrawTopClockBar(dt);
    eink.drawRect(12, 36, 176, 152, true);

    switch (id) {
        case APP_WALLET:
            eink.drawRect(22, 52, 156, 56, true);
            einkDrawTime(38, 66, 16, dt, g_settings.timeFormat24h);
            eink.drawRect(22, 120, 156, 50, true);
            eink.fillRect(30, 130, 64, 6, true);
            eink.fillRect(30, 142, 112, 4, true);
            eink.fillRect(30, 152, 78, 4, true);
            eink.drawRect(150, 128, 18, 18, true);
            eink.fillRect(156, 134, 6, 6, true);
            break;
        case APP_HEALTH:   einkDrawIconHeart(72, 86, 56); break;
        case APP_SETTINGS: einkDrawIconGear(72, 86, 56); break;
        case APP_NFC:      einkDrawIconLock(72, 86, 56); break;
        case APP_STATS:
            eink.fillRect(64, 126, 14, 28, true);
            eink.fillRect(90, 106, 14, 48, true);
            eink.fillRect(116, 90, 14, 64, true);
            break;
        case APP_ALARM:
            eink.drawRect(72, 94, 56, 56, true);
            eink.drawLine(100, 122, 100, 104, true);
            eink.drawLine(100, 122, 114, 122, true);
            break;
        case APP_GAME:
            for (int i = 0; i < 6; ++i) {
                eink.fillRect(60 + i * 12, 112 + ((i % 2) ? 10 : 0), 10, 10, true);
            }
            break;
        default:
            einkDrawMainGrid();
            break;
    }
}

static void renderEinkUi(AppId id, bool antiGhost = true, bool fastRefresh = false) {
    if (!eink.isReady()) {
        return;
    }

    g_einkCurrentApp = id;
    DateTime dt = SystemClock::instance().now();
    g_lastEinkRenderedMinute = dt.minute;
    g_lastEinkRenderedHour = dt.hour;

    eink.beginFrame(true);
    eink.drawRect(0, 0, 200, 200, true);

    if (id == APP_HOME) {
        einkDrawTopClockBar(dt);
        einkDrawMainGrid();
    } else if (id == APP_WATCHFACE) {
        switch (g_einkWatchfaceStyle % (uint8_t)WatchFaceStyle::STYLE_COUNT) {
            case 0: einkDrawWatchfaceDigital(dt); break;
            case 1: einkDrawWatchfaceAnalog(dt); break;
            case 2: einkDrawWatchfaceMinimal(dt); break;
            default: einkDrawWatchfaceSolana(dt); break;
        }
    } else {
        einkDrawAppFrame(id, dt);
    }

    eink.present(antiGhost, fastRefresh);
}
#endif

static bool openAppById(AppId id) {
#if !SOLWEAR_HAS_IMU
    if (id == APP_HEALTH) {
        return false;
    }
#endif
#if !SOLWEAR_HAS_NFC
    if (id == APP_NFC) {
        return false;
    }
#endif
#if !SOLWEAR_HAS_BATTERY
    if (id == APP_CHARGING) {
        return false;
    }
#endif

    App* cur = ScreenManager::instance().currentApp();
    if (cur && cur->getAppId() == id) {
        return true;
    }

    if (id == APP_WATCHFACE) {
        ScreenManager::instance().goHome();
        return true;
    }
    if (id == APP_HOME) {
        ScreenManager::instance().goHome();
        ScreenManager::instance().pushScreen(APP_HOME, Transition::SLIDE_LEFT);
        return true;
    }
    if (id == APP_CHARGING) {
        ScreenManager::instance().pushScreen(APP_CHARGING, Transition::SLIDE_UP);
        g_chargingScreenActive = true;
        return true;
    }

    ScreenManager::instance().pushScreen(id, Transition::SLIDE_LEFT);
    return true;
}

static void assertPowerLatchPins() {
#if defined(ARDUINO_ARCH_RP2040)
    // Some board revisions route the soft-latch to different GPIOs.
    // Driving known candidates HIGH is safe on this hardware and avoids
    // "stays on only while button is held" failures.
    const uint8_t latchPins[] = {14, 15, 18, 19};
    for (uint8_t pin : latchPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
#else
    pinMode(PIN_POWER_HOLD, OUTPUT);
    digitalWrite(PIN_POWER_HOLD, HIGH);
#endif
}

static void bootCheckpoint(const char* label) {
    Serial.printf("[BOOT][S%02u] %s (heap=%lu)\n",
                  g_bootStage++,
                  label,
                  (unsigned long)getFreeHeapBytes());
    Serial.flush();
}

// ============================================================
// App Factory Functions
// ============================================================
static App* createWatchFace() { return new WatchFaceApp(); }
static App* createHome()      { return new HomeApp(); }
static App* createSettings()  { return new SettingsApp(); }
static App* createWallet()    { return new WalletApp(); }
static App* createNfc()       { return new NfcApp(); }
static App* createHealth()    { return new HealthApp(); }
static App* createGame()      { return new GameApp(); }
static App* createAlarm()     { return new AlarmApp(); }
static App* createCharging()  { return new ChargingApp(); }
static App* createStats()     { return new StatsApp(); }

// Print a single status heartbeat the service tool can parse.
// Format: [STATUS] batt=<pct> volt=<v> heap=<bytes> steps=<n> uptime=<sec> charging=<0|1> temp=<C>
static void emitStatusHeartbeat() {
    uint8_t battPct = 0;
    float battVolt = 0.0f;
    bool charging = false;
    uint16_t battRaw = 0;
    float battDiv = BATTERY_DIVIDER;

#if SOLWEAR_HAS_BATTERY
    battPct = battery.getPercent();
    battVolt = battery.getVoltage();
    charging = battery.isCharging();
    battRaw = battery.getRawAdc();
    battDiv = battery.getDivider();
#endif

    uint32_t steps = 0;
#if SOLWEAR_HAS_IMU
    steps = imu.getSteps();
#endif

    Serial.printf("[STATUS] batt=%u volt=%.2f heap=%lu steps=%lu uptime=%lu charging=%d temp=%.1f raw=%u div=%.3f proto=%s mcu=%s display=%s caps=%s\n",
                  battPct,
                  battVolt,
                  (unsigned long)getFreeHeapBytes(),
                  (unsigned long)steps,
                  (unsigned long)(millis() / 1000),
                  charging ? 1 : 0,
                  thermal.getTemperatureC(),
                  battRaw,
                  battDiv,
                  SOLWEAR_PROTO_ID,
                  SOLWEAR_TARGET_MCU,
                  SOLWEAR_TARGET_DISPLAY,
                  SOLWEAR_PROTO_CAPS);
    Serial.flush();
}

// Handle a single command line received over USB CDC.
//   calbat <volts>   — calibrate battery divider against measured voltage
//   bri <percent>    — set backlight brightness
//   buzz test        — play a short buzzer test
//   buzz alarm       — play alarm pattern
//   buzz on|off      — enable/disable buzzer output
//   display probe    — run low-level panel color-fill probe
//   display sweep    — try alternate display control pin profiles
//   buzz sweep       — sweep candidate buzzer pins with tone test
//   diag on|off      — lock display awake/full-brightness for hardware tests
//   app <name>       — open watch app (watchface|home|settings|wallet|...)
//   nav home|back    — watch navigation helpers
//   set watchface N  — set watchface style (0..3) and persist
//   clock format 12|24 — set clock format
//   set wallpaper N  — set wallpaper index and persist
//   set stepgoal N   — set step goal and persist
//   time now         — print clock time/date
//   help             — list commands
static void handleSerialCommand(const char* line) {
    if (strncmp(line, "calbat ", 7) == 0) {
#if SOLWEAR_HAS_BATTERY
        float measured = atof(line + 7);
        float newDiv = battery.calibrate(measured);
        if (newDiv > 0.0f) {
            g_settings.batteryDivider = newDiv;
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CAL] battery divider = %.4f (saved)\n", newDiv);
        } else {
            Serial.println("[CAL] calibration FAILED — check measured value");
        }
#else
        Serial.println("[CAL] battery not enabled on this prototype");
#endif
    } else if (strncmp(line, "bri ", 4) == 0) {
        int b = atoi(line + 4);
        if (b >= 0 && b <= 100) {
#if !SOLWEAR_EINK_TARGET
            display.setBrightness((uint8_t)b);
#endif
            g_settings.brightness = (uint8_t)b;
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CMD] brightness = %d\n", b);
        }
    } else if (strcmp(line, "status now") == 0) {
        emitStatusHeartbeat();
    } else if (strcmp(line, "buzz test") == 0) {
#if SOLWEAR_HAS_BUZZER
        buzzer.setEnabled(true);
        buzzer.beep();
        Serial.println("[CMD] buzzer test beep");
#else
        Serial.println("[CMD] buzzer not enabled on this prototype");
#endif
    } else if (strcmp(line, "buzz alarm") == 0) {
#if SOLWEAR_HAS_BUZZER
        buzzer.setEnabled(true);
        buzzer.alarm();
        Serial.println("[CMD] buzzer alarm pattern");
#else
        Serial.println("[CMD] buzzer not enabled on this prototype");
#endif
    } else if (strcmp(line, "buzz on") == 0) {
#if SOLWEAR_HAS_BUZZER
        buzzer.setEnabled(true);
        g_settings.soundEnabled = true;
        Storage::instance().saveSettings(g_settings);
        Serial.println("[CMD] buzzer enabled (saved)");
#else
        Serial.println("[CMD] buzzer not enabled on this prototype");
#endif
    } else if (strcmp(line, "buzz off") == 0) {
#if SOLWEAR_HAS_BUZZER
        buzzer.setEnabled(false);
        buzzer.noTone();
        g_settings.soundEnabled = false;
        Storage::instance().saveSettings(g_settings);
        Serial.println("[CMD] buzzer disabled (saved)");
#else
        Serial.println("[CMD] buzzer not enabled on this prototype");
#endif
    } else if (strcmp(line, "display probe") == 0) {
    #if SOLWEAR_EINK_TARGET
        renderEinkUi(g_einkCurrentApp, true);
        Serial.println("[CMD] eink probe refresh done");
    #else
        power.setDiagnosticsMode(true);
        display.runPanelProbe();
        Serial.println("[CMD] display probe done");
    #endif
    } else if (strcmp(line, "display sweep") == 0) {
    #if SOLWEAR_EINK_TARGET
        eink.scrub();
        renderEinkUi(g_einkCurrentApp, false);
        Serial.println("[CMD] eink scrub done");
    #else
        power.setDiagnosticsMode(true);
        display.runPanelSweep();
        Serial.println("[CMD] display sweep done");
    #endif
    } else if (strcmp(line, "eink clear") == 0) {
    #if SOLWEAR_EINK_TARGET
        eink.clear(true);
        Serial.println("[CMD] eink clear done");
    #else
        Serial.println("[CMD] eink not enabled in this build");
    #endif
    } else if (strcmp(line, "eink test") == 0) {
    #if SOLWEAR_EINK_TARGET
        renderEinkUi(g_einkCurrentApp, true);
        Serial.println("[CMD] eink test redraw done");
    #else
        Serial.println("[CMD] eink not enabled in this build");
    #endif
    } else if (strcmp(line, "eink scrub") == 0) {
    #if SOLWEAR_EINK_TARGET
        eink.scrub();
        renderEinkUi(g_einkCurrentApp, false);
        Serial.println("[CMD] eink scrub done");
    #else
        Serial.println("[CMD] eink not enabled in this build");
    #endif
    } else if (strcmp(line, "buzz sweep") == 0) {
        power.setDiagnosticsMode(true);
        const uint8_t candidates[] = {20, 19, 18, 17, 16, 15, 14, 13, 12};
        Serial.println("[CMD] buzzer pin sweep start");
        for (uint8_t pin : candidates) {
            if (pin == PIN_LCD_BL || pin == PIN_LCD_CLK || pin == PIN_LCD_MOSI ||
                pin == PIN_LCD_MISO || pin == PIN_LCD_CS || pin == PIN_LCD_DC ||
                pin == PIN_LCD_RST) {
                continue;
            }
            Serial.printf("[CMD] buzz sweep pin=%u\n", (unsigned)pin);
            ::tone(pin, 1200);
            delay(180);
            ::noTone(pin);
            delay(120);
        }
        Serial.println("[CMD] buzzer pin sweep done");
    } else if (strcmp(line, "diag on") == 0) {
        power.setDiagnosticsMode(true);
        Serial.println("[CMD] diagnostics mode ON");
    } else if (strcmp(line, "diag off") == 0) {
        power.setDiagnosticsMode(false);
        Serial.println("[CMD] diagnostics mode OFF");
    } else if (strncmp(line, "app ", 4) == 0) {
        AppId target;
        if (parseAppIdToken(line + 4, target)) {
            if (openAppById(target)) {
#if SOLWEAR_EINK_TARGET
                renderEinkUi(target);
#endif
                Serial.printf("[CMD] app opened: %s\n", line + 4);
            } else {
                Serial.printf("[CMD] app unavailable on this prototype: %s\n", line + 4);
            }
        } else {
            Serial.printf("[CMD] unknown app: %s\n", line + 4);
        }
    } else if (strncmp(line, "tap ", 4) == 0) {
#if SOLWEAR_EINK_TARGET
        int x = -1;
        int y = -1;
        if (sscanf(line + 4, "%d %d", &x, &y) == 2) {
#if SOLWEAR_EINK_ROTATE_180
            x = 199 - x;
            y = 199 - y;
#endif
            bool handled = false;
            if (x >= 0 && x < 200 && y >= 0 && y < 200) {
                if (y <= 28) {
                    renderEinkUi(APP_WATCHFACE, false);
                    handled = true;
                } else if (y >= 168) {
                    renderEinkUi(APP_HOME, false);
                    handled = true;
                } else if (g_einkCurrentApp == APP_HOME) {
                    AppId target = APP_HOME;
                    if (einkMapTapToHomeApp((int16_t)x, (int16_t)y, target)) {
                        if (!openAppById(target)) {
                            target = APP_SETTINGS;
                            openAppById(target);
                        }
                        renderEinkUi(target, false);
                        handled = true;
                    }
                } else {
                    renderEinkUi(APP_HOME, false);
                    handled = true;
                }
            }
            if (!handled) {
                renderEinkUi(g_einkCurrentApp, false);
            }
            Serial.printf("[CMD] tap x=%d y=%d\n", x, y);
        } else {
            Serial.println("[CMD] usage: tap <x> <y>");
        }
#else
        Serial.println("[CMD] tap unsupported on this target");
#endif
    } else if (strcmp(line, "nav home") == 0 || strcmp(line, "home") == 0) {
        ScreenManager::instance().goHome();
#if SOLWEAR_EINK_TARGET
        renderEinkUi(APP_HOME);
#endif
        Serial.println("[CMD] nav home");
    } else if (strcmp(line, "nav back") == 0 || strcmp(line, "back") == 0) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
#if SOLWEAR_EINK_TARGET
        if (g_einkCurrentApp == APP_WATCHFACE) {
            renderEinkUi(APP_HOME);
        } else {
            renderEinkUi(APP_WATCHFACE);
        }
#endif
        Serial.println("[CMD] nav back");
    } else if (strncmp(line, "set watchface ", 14) == 0) {
        int idx = atoi(line + 14);
        if (idx >= 0 && idx < (int)WatchFaceStyle::STYLE_COUNT) {
            g_settings.watchFaceIndex = (uint8_t)idx;
#if SOLWEAR_EINK_TARGET
            g_einkWatchfaceStyle = g_settings.watchFaceIndex;
#endif
            if (g_watchface) {
                g_watchface->setStyle((WatchFaceStyle)g_settings.watchFaceIndex);
            }
            Storage::instance().saveSettings(g_settings);
#if SOLWEAR_EINK_TARGET
            renderEinkUi(APP_WATCHFACE, false);
#endif
            Serial.printf("[CMD] watchface = %d (saved)\n", idx);
        } else {
            Serial.println("[CMD] invalid watchface index (0..3)");
        }
    } else if (strcmp(line, "watchface next") == 0) {
#if SOLWEAR_EINK_TARGET
        g_einkWatchfaceStyle = (uint8_t)((g_einkWatchfaceStyle + 1) % (uint8_t)WatchFaceStyle::STYLE_COUNT);
        g_settings.watchFaceIndex = g_einkWatchfaceStyle;
        Storage::instance().saveSettings(g_settings);
        renderEinkUi(APP_WATCHFACE, false);
        Serial.printf("[CMD] watchface = %u\n", (unsigned)g_einkWatchfaceStyle);
#else
        Serial.println("[CMD] watchface next unsupported on this target");
#endif
    } else if (strncmp(line, "clock sync ", 11) == 0) {
        uint32_t epoch = (uint32_t)strtoul(line + 11, nullptr, 10);
        if (epoch >= 1609459200UL) {
            SystemClock::instance().setUnixEpoch(epoch);
#if SOLWEAR_EINK_TARGET
            renderEinkUi(g_einkCurrentApp, false);
#endif
            Serial.printf("[CMD] clock synced epoch=%lu\n", (unsigned long)epoch);
        } else {
            Serial.println("[CMD] invalid epoch");
        }
    } else if (strcmp(line, "clock format 12") == 0 || strcmp(line, "time format 12") == 0) {
        g_settings.timeFormat24h = false;
        Storage::instance().saveSettings(g_settings);
#if SOLWEAR_EINK_TARGET
        renderEinkUi(g_einkCurrentApp, false);
#endif
        Serial.println("[CMD] clock format = 12h");
    } else if (strcmp(line, "clock format 24") == 0 || strcmp(line, "time format 24") == 0) {
        g_settings.timeFormat24h = true;
        Storage::instance().saveSettings(g_settings);
#if SOLWEAR_EINK_TARGET
        renderEinkUi(g_einkCurrentApp, false);
#endif
        Serial.println("[CMD] clock format = 24h");
    } else if (strcmp(line, "clock format") == 0 || strcmp(line, "time format") == 0) {
        Serial.printf("[CMD] clock format = %s\n", g_settings.timeFormat24h ? "24h" : "12h");
    } else if (strncmp(line, "set wallpaper ", 14) == 0) {
        int idx = atoi(line + 14);
        if (idx >= 0 && idx <= 255) {
            g_settings.wallpaperIndex = (uint8_t)idx;
            if (g_watchface) {
                g_watchface->setWallpaperIndex(g_settings.wallpaperIndex);
            }
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CMD] wallpaper = %d (saved)\n", idx);
        } else {
            Serial.println("[CMD] invalid wallpaper index");
        }
    } else if (strncmp(line, "set stepgoal ", 13) == 0) {
        int goal = atoi(line + 13);
        if (goal >= 1000 && goal <= 50000) {
            g_settings.stepGoal = (uint16_t)goal;
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CMD] stepGoal = %d (saved)\n", goal);
        } else {
            Serial.println("[CMD] invalid stepGoal (1000..50000)");
        }
    } else if (strcmp(line, "time now") == 0 || strcmp(line, "clock now") == 0) {
        DateTime dt = SystemClock::instance().now();
        uint8_t hour = dt.hour;
        if (!g_settings.timeFormat24h) {
            hour = (uint8_t)(hour % 12);
            if (hour == 0) {
                hour = 12;
            }
        }
        Serial.printf("[CMD] time %04u-%02u-%02u %02u:%02u:%02u %s\n",
                      dt.year,
                      dt.month,
                      dt.day,
                      hour,
                      dt.minute,
                      dt.second,
                      g_settings.timeFormat24h ? "24h" : (dt.hour >= 12 ? "PM" : "AM"));
    } else if (strcmp(line, "version") == 0) {
        Serial.printf("[CMD] SolWearOS v1.0 proto=%s mcu=%s display=%s caps=%s\n",
                      SOLWEAR_PROTO_ID,
                      SOLWEAR_TARGET_MCU,
                      SOLWEAR_TARGET_DISPLAY,
                      SOLWEAR_PROTO_CAPS);
    } else if (strcmp(line, "power off") == 0 || strcmp(line, "poweroff") == 0) {
        Serial.println("[CMD] power off requested");
        Serial.flush();
        delay(20);
        power.holdOff();
    } else if (strcmp(line, "reboot bootsel") == 0) {
        Serial.println("[CMD] rebooting to bootloader...");
        Serial.flush();
        delay(20);
#if defined(ARDUINO_ARCH_RP2040)
        rp2040.rebootToBootloader();
#elif defined(ARDUINO_ARCH_ESP32)
        ESP.restart();
#endif
    } else if (strcmp(line, "help") == 0) {
        Serial.println("[CMD] commands: calbat <volts>, bri <0-100>, status now, buzz test|buzz alarm|buzz on|buzz off|buzz sweep, display probe|display sweep, eink test|eink clear|eink scrub, diag on|diag off, app <name>, tap <x> <y>, nav home|back, set watchface <0-3>|watchface next, clock sync <epoch>|clock format 12|24|time now, set wallpaper <n>, set stepgoal <1000-50000>, version, power off|poweroff, reboot bootsel, help");
    } else if (line[0] != '\0') {
        Serial.printf("[CMD] unknown: '%s'\n", line);
    }
}

static void pollSerialCommands() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            g_serialCmdBuf[g_serialCmdLen] = '\0';
            handleSerialCommand(g_serialCmdBuf);
            g_serialCmdLen = 0;
        } else if (g_serialCmdLen < sizeof(g_serialCmdBuf) - 1) {
            g_serialCmdBuf[g_serialCmdLen++] = c;
        }
    }
}

static void pollPowerButtonLongPress(uint32_t dt) {
    if (!g_powerKeyEnabled) return;

    bool pressed = false;
    if (g_powerKeySharesLatchPin) {
        // Shared latch/key net: briefly sample as input, then immediately restore hold HIGH.
        pinMode(PIN_POWER_HOLD, INPUT_PULLUP);
        delayMicroseconds(50);
        pressed = (digitalRead(PIN_POWER_HOLD) == LOW);
        pinMode(PIN_POWER_HOLD, OUTPUT);
        digitalWrite(PIN_POWER_HOLD, HIGH);
    } else {
        pressed = (digitalRead(PIN_POWER_KEY) == LOW);
    }

    if (pressed) {
        if (g_powerBtnHeldMs == 0) {
            Serial.println("[PWR] button press detected");
        }
        g_powerBtnHeldMs += dt;
        if (g_powerBtnHeldMs >= POWER_HOLD_OFF_MS) {
            Serial.println("[PWR] long-press shutdown");
            Serial.flush();
            delay(20);
            power.holdOff();
        }
    } else if (g_powerBtnHeldMs != 0) {
        Serial.printf("[PWR] button released after %lums\n", (unsigned long)g_powerBtnHeldMs);
        g_powerBtnHeldMs = 0;
    }
}

// ============================================================
// Setup
// ============================================================
void setup() {
    // CRITICAL: assert soft-power latch candidates before any other init.
    assertPowerLatchPins();

    Serial.begin(115200);
    delay(100);
    Serial.printf("\n=== SolWearOS v1.0 proto=%s mcu=%s display=%s ===\n",
                  SOLWEAR_PROTO_ID,
                  SOLWEAR_TARGET_MCU,
                  SOLWEAR_TARGET_DISPLAY);
    Serial.println("[BOOT] Starting...");
    Serial.printf("[BOOT] Power latch asserted (primary GP%d)\n", PIN_POWER_HOLD);
#if PIN_POWER_KEY >= 0
    if (PIN_POWER_KEY == PIN_POWER_HOLD) {
        g_powerKeyEnabled = true;
        g_powerKeySharesLatchPin = true;
        Serial.printf("[BOOT] Power-key long-press enabled on shared GP%d (%lums)\n",
                      PIN_POWER_KEY,
                      (unsigned long)POWER_HOLD_OFF_MS);
    } else {
        pinMode(PIN_POWER_KEY, INPUT_PULLUP);
        g_powerKeyEnabled = true;
        Serial.printf("[BOOT] Power-key long-press enabled on GP%d (%lums)\n",
                      PIN_POWER_KEY,
                      (unsigned long)POWER_HOLD_OFF_MS);
    }
#else
    Serial.println("[BOOT] Power-key long-press disabled: PIN_POWER_KEY not configured");
#endif
    Serial.flush();
    bootCheckpoint("serial ready");

    // Initialize I2C1 only when at least one I2C1 peripheral is enabled.
#if SOLWEAR_HAS_TOUCH || SOLWEAR_HAS_IMU
#if defined(ARDUINO_ARCH_RP2040)
    Wire1.setSDA(PIN_TOUCH_SDA);
    Wire1.setSCL(PIN_TOUCH_SCL);
    Wire1.begin();
#else
    Wire1.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000U);
#endif
    Wire1.setClock(400000);  // 400kHz fast mode
    bootCheckpoint("i2c1 ready");
#else
    bootCheckpoint("i2c1 skipped");
#endif

    // Initialize display first so we can show errors visually.
    Serial.println("[HAL] Display...");
    Serial.flush();
#if SOLWEAR_EINK_TARGET
    eink.init();
    if (eink.isReady()) {
        renderEinkUi(APP_HOME);
        bootCheckpoint("eink ready");
    } else {
        bootCheckpoint("eink not ready");
    }
#else
    display.init();
    bootCheckpoint(display.isReady() ? "display ready" : "display not ready");

    if (!display.isReady()) {
        Serial.println("[BOOT] FATAL: display not ready, halting render loop");
        // Don't halt — keep serial alive for service tool diagnosis.
    }

    // Show boot splash (only if a sprite was allocated)
    if (display.isReady()) {
        TFT_eSprite& canvas = display.getCanvas();
        canvas.fillSprite(TFT_BLACK);
        canvas.setTextColor(0x9C1F);  // Solana purple
        canvas.drawString("SolWearOS", 60, 100, 4);
        canvas.setTextColor(0x07FF);
        canvas.drawString("v1.0", 100, 140, 2);
        canvas.setTextColor(0x7BEF);
        canvas.drawString("Initializing...", 65, 200, 2);
        display.pushCanvas();
    }
#endif

#if SOLWEAR_HAS_TOUCH
    Serial.println("[HAL] Touch...");   Serial.flush();
    touch.init();
    bootCheckpoint("touch init done");
#else
    Serial.println("[HAL] Touch skipped");
    bootCheckpoint("touch skipped");
#endif

#if SOLWEAR_HAS_IMU
    Serial.println("[HAL] IMU...");     Serial.flush();
    imu.init();
    bootCheckpoint("imu init done");
#else
    Serial.println("[HAL] IMU skipped");
    bootCheckpoint("imu skipped");
#endif

#if SOLWEAR_HAS_BUZZER
    Serial.println("[HAL] Buzzer...");  Serial.flush();
    buzzer.init();
    bootCheckpoint("buzzer init done");
#else
    Serial.println("[HAL] Buzzer skipped");
    bootCheckpoint("buzzer skipped");
#endif

#if SOLWEAR_HAS_BATTERY
    Serial.println("[HAL] Battery..."); Serial.flush();
    battery.init();
    bootCheckpoint("battery init done");
#else
    Serial.println("[HAL] Battery skipped");
    bootCheckpoint("battery skipped");
#endif

    Serial.println("[HAL] Power...");   Serial.flush();
    power.init();
    bootCheckpoint("power init done");

    Serial.println("[HAL] Thermal..."); Serial.flush();
    thermal.init();
    Serial.printf("[HAL] CPU temp = %.1f C\n", thermal.getTemperatureC());
    bootCheckpoint("thermal init done");

    // NOTE: NFC is intentionally NOT brought up at boot. The PN532 only
    // powers on when the user opens the NFC app or wallet starts a tx.
    Serial.println("[HAL] NFC...");     Serial.flush();
    nfc.init();  // no-op, prints "lazy mode"
    bootCheckpoint("nfc init done");

    Serial.println("[CORE] Storage..."); Serial.flush();
    Storage::instance().init();
    bootCheckpoint("storage init done");

    if (Storage::instance().loadSettings(g_settings)) {
#if SOLWEAR_HAS_BATTERY
        if (g_settings.batteryDivider < 1.0f || g_settings.batteryDivider > 16.0f) {
            Serial.printf("[CORE] Invalid battery divider %.3f, resetting to %.3f\n",
                          g_settings.batteryDivider,
                          BATTERY_DIVIDER);
            g_settings.batteryDivider = BATTERY_DIVIDER;
            Storage::instance().saveSettings(g_settings);
        }
#endif
    #if !SOLWEAR_EINK_TARGET
        display.setBrightness(g_settings.brightness);
    #endif
#if SOLWEAR_HAS_BUZZER
        buzzer.setEnabled(g_settings.soundEnabled);
#endif
#if SOLWEAR_HAS_BATTERY
        battery.setDivider(g_settings.batteryDivider);
        Serial.printf("[CORE] Settings loaded (battery divider=%.3f)\n",
                      g_settings.batteryDivider);
#else
        Serial.println("[CORE] Settings loaded");
#endif
#if SOLWEAR_EINK_TARGET
    g_einkWatchfaceStyle = (uint8_t)(g_settings.watchFaceIndex % (uint8_t)WatchFaceStyle::STYLE_COUNT);
#endif
    }

    SystemClock::instance().init();
    bootCheckpoint("clock init done");

    // Register all apps
    Serial.println("[CORE] Registering apps...");
    auto& reg = AppRegistry::instance();
    reg.registerApp(APP_WATCHFACE, "Clock",    nullptr, createWatchFace);
    reg.registerApp(APP_HOME,     "Home",     nullptr, createHome);
    reg.registerApp(APP_SETTINGS, "Settings", nullptr, createSettings);
    reg.registerApp(APP_WALLET,   "Wallet",   nullptr, createWallet);
#if SOLWEAR_HAS_NFC
    reg.registerApp(APP_NFC,      "NFC",      nullptr, createNfc);
#endif
#if SOLWEAR_HAS_IMU
    reg.registerApp(APP_HEALTH,   "Health",   nullptr, createHealth);
#endif
    reg.registerApp(APP_GAME,     "Snake",    nullptr, createGame);
    reg.registerApp(APP_ALARM,    "Alarm",    nullptr, createAlarm);
    reg.registerApp(APP_CHARGING, "Charging", nullptr, createCharging);
    reg.registerApp(APP_STATS,    "Stats",    nullptr, createStats);
    bootCheckpoint("apps registered");

    // Create and initialize watchface as root screen
    g_watchface = new WatchFaceApp();
    g_watchface->setStyle((WatchFaceStyle)g_settings.watchFaceIndex);
    g_watchface->setWallpaperIndex(g_settings.wallpaperIndex);
    ScreenManager::instance().init(g_watchface);
    bootCheckpoint("screen manager ready");

    // If we booted on USB power, show the charging screen immediately.
#if SOLWEAR_HAS_BATTERY
    battery.update();
    if (battery.isCharging()) {
        Serial.println("[BOOT] On charger — pushing charging screen");
        ScreenManager::instance().pushScreen(APP_CHARGING, Transition::SLIDE_UP);
        g_chargingScreenActive = true;
    }
#endif

    // Boot sound
#if SOLWEAR_HAS_BUZZER
    buzzer.playMelody(Sounds::BOOT, Sounds::BOOT_LEN);
    bootCheckpoint("boot sound queued");
#else
    bootCheckpoint("boot sound skipped");
#endif

    g_lastFrameTime = millis();
    Serial.println("=== Boot complete ===");
    Serial.printf("Free heap: %lu bytes\n", (unsigned long)getFreeHeapBytes());
    emitStatusHeartbeat();
    Serial.flush();
}

// ============================================================
// Main Loop (UI + Events @ 30fps)
// ============================================================
void loop() {
    uint32_t now = millis();
    uint32_t dt = now - g_lastFrameTime;

    // Frame rate limiting — yield to systick / WFI between ticks instead
    // of busy-spinning. delay() on earlephilhower already calls
    // tight_loop_contents() which lets the core sleep.
    if (dt < FRAME_TIME_MS) {
        delay(FRAME_TIME_MS - dt);
        now = millis();
        dt = now - g_lastFrameTime;
    }
    g_lastFrameTime = now;

    // Drain any commands from the service tool first.
    pollSerialCommands();
    pollPowerButtonLongPress(dt);

    SystemClock::instance().update();

#if SOLWEAR_EINK_TARGET
    DateTime nowDt = SystemClock::instance().now();
    if ((g_einkCurrentApp == APP_HOME || g_einkCurrentApp == APP_WATCHFACE) &&
        nowDt.minute != g_lastEinkRenderedMinute) {
        const bool hourBoundary = nowDt.hour != g_lastEinkRenderedHour;
        const bool periodicAntiGhost = (nowDt.minute % 10) == 0;
        const bool antiGhost = hourBoundary || periodicAntiGhost;
        renderEinkUi(g_einkCurrentApp, antiGhost, !antiGhost);
    }
#endif

    // --- Touch ---
#if SOLWEAR_HAS_TOUCH
    TouchEvent touchEvent;
    if (touch.poll(touchEvent)) {
        power.registerActivity();
        Event ev;
        ev.type = EventType::TOUCH;
        ev.timestamp = now;
        ev.touch = touchEvent;
        ScreenManager::instance().handleEvent(ev);
    } else if (touch.isTouching()) {
        power.registerActivity();
    }
#endif

    // --- IMU step detection ---
#if SOLWEAR_HAS_IMU
    g_imuTimer += dt;
    if (g_imuTimer >= IMU_POLL_MS) {
        g_imuTimer = 0;
        if (imu.detectStep()) {
            Event ev;
            ev.type = EventType::STEP_DETECTED;
            ev.timestamp = now;
            ev.step.totalSteps = imu.getSteps();
            EventSystem::instance().post(ev);
        }
    }
#endif

    // --- Battery ADC + charging detection ---
#if SOLWEAR_HAS_BATTERY
    g_batteryTimer += dt;
    if (g_batteryTimer >= BATTERY_POLL_MS) {
        g_batteryTimer = 0;
        battery.update();

        Event bev;
        bev.type = EventType::BATTERY_UPDATE;
        bev.timestamp = now;
        bev.battery.percent = battery.getPercent();
        bev.battery.voltage = battery.getVoltage();
        EventSystem::instance().post(bev);

        if (battery.isCritical()) {
            Serial.println("[BATT] CRITICAL");
        }

        // Auto-push charging screen when USB plugged in mid-use.
        // Auto-pop is handled inside ChargingApp::update().
        if (battery.isCharging() && !g_chargingScreenActive) {
            App* cur = ScreenManager::instance().currentApp();
            if (!cur || cur->getAppId() != APP_CHARGING) {
                ScreenManager::instance().pushScreen(APP_CHARGING, Transition::SLIDE_UP);
                g_chargingScreenActive = true;
            }
        } else if (!battery.isCharging()) {
            g_chargingScreenActive = false;
        }
    }
#endif

    // --- Thermal poll (every 1s) + power throttling ---
    g_thermalTimer += dt;
    if (g_thermalTimer >= 1000) {
        g_thermalTimer = 0;
        thermal.update();
        power.onTemperatureUpdate(thermal.getTemperatureC());
    }

    // --- Status heartbeat for service tool (every 5s) ---
    g_statusTimer += dt;
    if (g_statusTimer >= 5000) {
        g_statusTimer = 0;
        emitStatusHeartbeat();
    }

    // --- Process event queue ---
    Event ev;
    while (EventSystem::instance().poll(ev)) {
        ScreenManager::instance().handleEvent(ev);
    }

    // --- Update subsystems ---
    power.update();
#if SOLWEAR_HAS_BUZZER
    buzzer.update();
#endif
    TimerManager::instance().update();
    ScreenManager::instance().update(dt);

    // --- Render (only if display is ready and on) ---
    if (display.isReady() && power.isDisplayOn()) {
        TFT_eSprite& canvas = display.getCanvas();

        App* app = ScreenManager::instance().currentApp();
        if (app) {
            canvas.fillSprite(TFT_BLACK);
            ScreenManager::instance().render(canvas);
            if (app->wantsStatusBar()) {
                statusBar.render(canvas);
            }
        }

        display.pushCanvas();
    }
}
