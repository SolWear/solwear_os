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
#include "hal/hal_thermal.h"

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

static bool openAppById(AppId id) {
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
    // Some board revisions route the soft-latch to different GPIOs.
    // Driving known candidates HIGH is safe on this hardware and avoids
    // "stays on only while button is held" failures.
    const uint8_t latchPins[] = {14, 15, 18, 19};
    for (uint8_t pin : latchPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
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
    Serial.printf("[STATUS] batt=%u volt=%.2f heap=%lu steps=%lu uptime=%lu charging=%d temp=%.1f raw=%u div=%.3f proto=%s mcu=%s display=%s caps=%s\n",
                  battery.getPercent(),
                  battery.getVoltage(),
                  (unsigned long)getFreeHeapBytes(),
                  (unsigned long)imu.getSteps(),
                  (unsigned long)(millis() / 1000),
                  battery.isCharging() ? 1 : 0,
                  thermal.getTemperatureC(),
                  battery.getRawAdc(),
                  battery.getDivider(),
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
//   set watchface N  — set watchface style (0..2) and persist
//   set wallpaper N  — set wallpaper index and persist
//   set stepgoal N   — set step goal and persist
//   help             — list commands
static void handleSerialCommand(const char* line) {
    if (strncmp(line, "calbat ", 7) == 0) {
        float measured = atof(line + 7);
        float newDiv = battery.calibrate(measured);
        if (newDiv > 0.0f) {
            g_settings.batteryDivider = newDiv;
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CAL] battery divider = %.4f (saved)\n", newDiv);
        } else {
            Serial.println("[CAL] calibration FAILED — check measured value");
        }
    } else if (strncmp(line, "bri ", 4) == 0) {
        int b = atoi(line + 4);
        if (b >= 0 && b <= 100) {
            display.setBrightness((uint8_t)b);
            g_settings.brightness = (uint8_t)b;
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CMD] brightness = %d\n", b);
        }
    } else if (strcmp(line, "status now") == 0) {
        emitStatusHeartbeat();
    } else if (strcmp(line, "buzz test") == 0) {
        buzzer.setEnabled(true);
        buzzer.beep();
        Serial.println("[CMD] buzzer test beep");
    } else if (strcmp(line, "buzz alarm") == 0) {
        buzzer.setEnabled(true);
        buzzer.alarm();
        Serial.println("[CMD] buzzer alarm pattern");
    } else if (strcmp(line, "buzz on") == 0) {
        buzzer.setEnabled(true);
        g_settings.soundEnabled = true;
        Storage::instance().saveSettings(g_settings);
        Serial.println("[CMD] buzzer enabled (saved)");
    } else if (strcmp(line, "buzz off") == 0) {
        buzzer.setEnabled(false);
        buzzer.noTone();
        g_settings.soundEnabled = false;
        Storage::instance().saveSettings(g_settings);
        Serial.println("[CMD] buzzer disabled (saved)");
    } else if (strcmp(line, "display probe") == 0) {
        power.setDiagnosticsMode(true);
        display.runPanelProbe();
        Serial.println("[CMD] display probe done");
    } else if (strcmp(line, "display sweep") == 0) {
        power.setDiagnosticsMode(true);
        display.runPanelSweep();
        Serial.println("[CMD] display sweep done");
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
            openAppById(target);
            Serial.printf("[CMD] app opened: %s\n", line + 4);
        } else {
            Serial.printf("[CMD] unknown app: %s\n", line + 4);
        }
    } else if (strcmp(line, "nav home") == 0 || strcmp(line, "home") == 0) {
        ScreenManager::instance().goHome();
        Serial.println("[CMD] nav home");
    } else if (strcmp(line, "nav back") == 0 || strcmp(line, "back") == 0) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
        Serial.println("[CMD] nav back");
    } else if (strncmp(line, "set watchface ", 14) == 0) {
        int idx = atoi(line + 14);
        if (idx >= 0 && idx < (int)WatchFaceStyle::STYLE_COUNT) {
            g_settings.watchFaceIndex = (uint8_t)idx;
            if (g_watchface) {
                g_watchface->setStyle((WatchFaceStyle)g_settings.watchFaceIndex);
            }
            Storage::instance().saveSettings(g_settings);
            Serial.printf("[CMD] watchface = %d (saved)\n", idx);
        } else {
            Serial.println("[CMD] invalid watchface index (0..2)");
        }
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
        Serial.println("[CMD] commands: calbat <volts>, bri <0-100>, status now, buzz test|buzz alarm|buzz on|buzz off|buzz sweep, display probe|display sweep, diag on|diag off, app <name>, nav home|back, set watchface <0-2>, set wallpaper <n>, set stepgoal <1000-50000>, version, power off|poweroff, reboot bootsel, help");
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
    Serial.printf("[BOOT] Power latch asserted (primary GP%d, candidates 15/18/19)\n", PIN_POWER_HOLD);
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

    // Initialize I2C1 (touch + IMU)
    Wire1.setSDA(PIN_TOUCH_SDA);
    Wire1.setSCL(PIN_TOUCH_SCL);
    Wire1.begin();
    Wire1.setClock(400000);  // 400kHz fast mode
    bootCheckpoint("i2c1 ready");

    // Initialize display first so we can show errors visually.
    Serial.println("[HAL] Display...");
    Serial.flush();
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

    Serial.println("[HAL] Touch...");   Serial.flush();
    touch.init();
    bootCheckpoint("touch init done");

    Serial.println("[HAL] IMU...");     Serial.flush();
    imu.init();
    bootCheckpoint("imu init done");

    Serial.println("[HAL] Buzzer...");  Serial.flush();
    buzzer.init();
    bootCheckpoint("buzzer init done");

    Serial.println("[HAL] Battery..."); Serial.flush();
    battery.init();
    bootCheckpoint("battery init done");

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
        if (g_settings.batteryDivider < 1.0f || g_settings.batteryDivider > 16.0f) {
            Serial.printf("[CORE] Invalid battery divider %.3f, resetting to %.3f\n",
                          g_settings.batteryDivider,
                          BATTERY_DIVIDER);
            g_settings.batteryDivider = BATTERY_DIVIDER;
            Storage::instance().saveSettings(g_settings);
        }
        display.setBrightness(g_settings.brightness);
        buzzer.setEnabled(g_settings.soundEnabled);
        battery.setDivider(g_settings.batteryDivider);
        Serial.printf("[CORE] Settings loaded (battery divider=%.3f)\n",
                      g_settings.batteryDivider);
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
    reg.registerApp(APP_NFC,      "NFC",      nullptr, createNfc);
    reg.registerApp(APP_HEALTH,   "Health",   nullptr, createHealth);
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
    battery.update();
    if (battery.isCharging()) {
        Serial.println("[BOOT] On charger — pushing charging screen");
        ScreenManager::instance().pushScreen(APP_CHARGING, Transition::SLIDE_UP);
        g_chargingScreenActive = true;
    }

    // Boot sound
    buzzer.playMelody(Sounds::BOOT, Sounds::BOOT_LEN);
    bootCheckpoint("boot sound queued");

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
