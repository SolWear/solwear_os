// ============================================================
// SolWearOS — Smartwatch OS for RP2040
// Main Entry Point
//
// Single-core (core 0) — UI rendering, touch, IMU, battery, events @ 30fps.
// FreeRTOS in earlephilhower v5.x conflicts with setup1/loop1 stacks.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// HAL
#include "hal/hal_display.h"
#include "hal/hal_touch.h"
#include "hal/hal_imu.h"
#include "hal/hal_nfc.h"
#include "hal/hal_buzzer.h"
#include "hal/hal_battery.h"
#include "hal/hal_power.h"

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
static bool g_chargingScreenActive = false;

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
// Format: [STATUS] batt=<pct> volt=<v> heap=<bytes> steps=<n> uptime=<sec> charging=<0|1>
static void emitStatusHeartbeat() {
    Serial.printf("[STATUS] batt=%u volt=%.2f heap=%lu steps=%lu uptime=%lu charging=%d\n",
                  battery.getPercent(),
                  battery.getVoltage(),
                  (unsigned long)rp2040.getFreeHeap(),
                  (unsigned long)imu.getSteps(),
                  (unsigned long)(millis() / 1000),
                  battery.isCharging() ? 1 : 0);
    Serial.flush();
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== SolWearOS v1.0 ===");
    Serial.println("[BOOT] Starting...");
    Serial.flush();

    // Initialize I2C1 (touch + IMU)
    Wire1.setSDA(PIN_TOUCH_SDA);
    Wire1.setSCL(PIN_TOUCH_SCL);
    Wire1.begin();
    Wire1.setClock(400000);  // 400kHz fast mode

    // Initialize display first so we can show errors visually.
    Serial.println("[HAL] Display...");
    Serial.flush();
    display.init();

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

    Serial.println("[HAL] Touch...");   Serial.flush();
    touch.init();

    Serial.println("[HAL] IMU...");     Serial.flush();
    imu.init();

    Serial.println("[HAL] Buzzer...");  Serial.flush();
    buzzer.init();

    Serial.println("[HAL] Battery..."); Serial.flush();
    battery.init();

    Serial.println("[HAL] Power...");   Serial.flush();
    power.init();

    // NOTE: NFC is intentionally NOT brought up at boot. The PN532 only
    // powers on when the user opens the NFC app or wallet starts a tx.
    Serial.println("[HAL] NFC...");     Serial.flush();
    nfc.init();  // no-op, prints "lazy mode"

    Serial.println("[CORE] Storage..."); Serial.flush();
    Storage::instance().init();

    if (Storage::instance().loadSettings(g_settings)) {
        display.setBrightness(g_settings.brightness);
        buzzer.setEnabled(g_settings.soundEnabled);
        Serial.println("[CORE] Settings loaded");
    }

    SystemClock::instance().init();

    // Register all apps
    Serial.println("[CORE] Registering apps...");
    auto& reg = AppRegistry::instance();
    reg.registerApp(APP_WATCHFACE, "Clock",    nullptr, createWatchFace);
    reg.registerApp(APP_HOME,     "Home",     nullptr, createHome);
    reg.registerApp(APP_SETTINGS, "Settings", nullptr, createSettings);
    reg.registerApp(APP_WALLET,   "Wallet",   nullptr, createWallet);
    reg.registerApp(APP_NFC,      "NFC",      nullptr, createNfc);
    reg.registerApp(APP_HEALTH,   "Health",   nullptr, createHealth);
    reg.registerApp(APP_GAME,     "Snake",    nullptr, createGame);
    reg.registerApp(APP_ALARM,    "Alarm",    nullptr, createAlarm);
    reg.registerApp(APP_CHARGING, "Charging", nullptr, createCharging);
    reg.registerApp(APP_STATS,    "Stats",    nullptr, createStats);

    // Create and initialize watchface as root screen
    g_watchface = new WatchFaceApp();
    g_watchface->setStyle((WatchFaceStyle)g_settings.watchFaceIndex);
    g_watchface->setWallpaperIndex(g_settings.wallpaperIndex);
    ScreenManager::instance().init(g_watchface);

    // If we booted on USB power, show the charging screen immediately.
    battery.update();
    if (battery.isCharging()) {
        Serial.println("[BOOT] On charger — pushing charging screen");
        ScreenManager::instance().pushScreen(APP_CHARGING, Transition::SLIDE_UP);
        g_chargingScreenActive = true;
    }

    // Boot sound
    buzzer.playMelody(Sounds::BOOT, Sounds::BOOT_LEN);

    g_lastFrameTime = millis();
    Serial.println("=== Boot complete ===");
    Serial.printf("Free heap: %d bytes\n", rp2040.getFreeHeap());
    Serial.flush();
}

// ============================================================
// Main Loop (UI + Events @ 30fps)
// ============================================================
void loop() {
    uint32_t now = millis();
    uint32_t dt = now - g_lastFrameTime;

    // Frame rate limiting
    if (dt < FRAME_TIME_MS) {
        delay(FRAME_TIME_MS - dt);
        now = millis();
        dt = now - g_lastFrameTime;
    }
    g_lastFrameTime = now;

    SystemClock::instance().update();

    // --- Touch ---
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

    // --- IMU step detection ---
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

    // --- Battery ADC + charging detection ---
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
    buzzer.update();
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
