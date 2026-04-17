#pragma once

// ============================================================
// SolWearOS Master Configuration
// Waveshare RP2040-Touch-LCD-1.69
// ============================================================

// --- Prototype identity (override via build_flags per environment) ---
#ifndef SOLWEAR_PROTO_ID
#define SOLWEAR_PROTO_ID "prototype-a-rp2040-lcd169"
#endif

#ifndef SOLWEAR_TARGET_MCU
#define SOLWEAR_TARGET_MCU "rp2040"
#endif

#ifndef SOLWEAR_TARGET_DISPLAY
#define SOLWEAR_TARGET_DISPLAY "st7789-240x280"
#endif

#ifndef SOLWEAR_GRAYSCALE_UI
#define SOLWEAR_GRAYSCALE_UI 0
#endif

#ifndef SOLWEAR_PROTO_CAPS
#define SOLWEAR_PROTO_CAPS "status,watch-control,apps,diagnostics,uf2"
#endif

#ifndef SOLWEAR_EINK_TARGET
#define SOLWEAR_EINK_TARGET 0
#endif

#ifndef SOLWEAR_EINK_ROTATE_180
#define SOLWEAR_EINK_ROTATE_180 0
#endif

#ifndef SOLWEAR_HAS_TOUCH
#define SOLWEAR_HAS_TOUCH 1
#endif

#ifndef SOLWEAR_HAS_BUTTONS
#define SOLWEAR_HAS_BUTTONS 0
#endif

#ifndef SOLWEAR_HAS_IMU
#define SOLWEAR_HAS_IMU 1
#endif

#ifndef SOLWEAR_HAS_NFC
#define SOLWEAR_HAS_NFC 1
#endif

#ifndef SOLWEAR_HAS_BATTERY
#define SOLWEAR_HAS_BATTERY 1
#endif

#ifndef SOLWEAR_HAS_BUZZER
#define SOLWEAR_HAS_BUZZER 1
#endif

// --- Display (ST7789V2 via SPI1) ---
#define PIN_LCD_DC      8
#define PIN_LCD_CS      9
#define PIN_LCD_CLK     10
#define PIN_LCD_MOSI    11
#define PIN_LCD_MISO    12
#define PIN_LCD_RST     13
#define PIN_LCD_BL      25

// --- E-ink profile pins (Prototype D, ESP32 v2 + Waveshare e-ink 1.54) ---
// These are currently used for diagnostics / profile metadata and future
// display backend work; primary LCD firmware path remains unchanged.
#ifndef PIN_EINK_DIN
#define PIN_EINK_DIN    15
#endif
#ifndef PIN_EINK_CLK
#define PIN_EINK_CLK    2
#endif
#ifndef PIN_EINK_CS
#define PIN_EINK_CS     4
#endif
#ifndef PIN_EINK_DC
#define PIN_EINK_DC     5
#endif
#ifndef PIN_EINK_RST
#define PIN_EINK_RST    18
#endif
#ifndef PIN_EINK_BUSY
#define PIN_EINK_BUSY   19
#endif

// --- Hardware Buttons (Prototype 2 / E: ESP32-S3 + 4 Buttons on display board)
// K1..K4 are the four tact switches on the ST7789 1.3" module.
#ifndef PIN_BUTTON_UP
#define PIN_BUTTON_UP       13 // K1
#endif
#ifndef PIN_BUTTON_DOWN
#define PIN_BUTTON_DOWN     12 // K2
#endif
#ifndef PIN_BUTTON_HASH
#define PIN_BUTTON_HASH     11 // K3
#endif
#ifndef PIN_BUTTON_STAR
#define PIN_BUTTON_STAR     10 // K4
#endif

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH    240
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT   280
#endif

// --- Touch (CST816S via I2C1) ---
#define PIN_TOUCH_SDA   6
#define PIN_TOUCH_SCL   7
#define PIN_TOUCH_INT   21
#define PIN_TOUCH_RST   22
#define TOUCH_I2C_ADDR  0x15

// --- IMU (QMI8658 via I2C1, shared bus with touch) ---
#define PIN_IMU_SDA     6
#define PIN_IMU_SCL     7
#define PIN_IMU_INT1    23
#define PIN_IMU_INT2    24
#define IMU_I2C_ADDR    0x6B

// --- NFC (PN532 via I2C0, separate bus) ---
#ifndef PIN_NFC_SDA
#define PIN_NFC_SDA     16
#endif
#ifndef PIN_NFC_SCL
#define PIN_NFC_SCL     17
#endif
#ifndef NFC_I2C_ADDR
#define NFC_I2C_ADDR    0x24  // or 0x48 / default
#endif

// --- Battery (ADC) ---
// RP2040 default is GP29 (VSYS/3). ESP32-S3 override comes from build_flags
// (Prototype 2 uses GP2 with an external 100K/100K divider from TP4056 B+).
#ifndef PIN_BATTERY_ADC
#define PIN_BATTERY_ADC 29
#endif
#define BATTERY_ADC_CH  3

// --- Buzzer (PWM) ---
#define PIN_BUZZER      20

// --- Soft power latch ---
// MCU must drive this HIGH to keep the 3.3V rail enabled after the
// power button is released. Default GP14. If the watch still dies on
// button release, try 15 → 18 → 19 (reflash, retry).
#define PIN_POWER_HOLD  14
// Optional separate GPIO for reading physical power-key state.
// On this watch, power key is on the same net as power hold (GP14 by default).
// Key is assumed active LOW.
#define PIN_POWER_KEY   PIN_POWER_HOLD

// --- UART (debug / future BLE) ---
#define PIN_UART_TX     0
#define PIN_UART_RX     1

// --- Spare GPIOs ---
#define PIN_SPARE_0     26
#define PIN_SPARE_1     27
#define PIN_SPARE_2     28

// ============================================================
// UI Layout Constants
// ============================================================
#define STATUS_BAR_HEIGHT   24
#define APP_AREA_Y          STATUS_BAR_HEIGHT
#define APP_AREA_HEIGHT     (SCREEN_HEIGHT - STATUS_BAR_HEIGHT)

#define ICON_SIZE           48
#define ICON_GRID_COLS      4
#define ICON_GRID_ROWS      3
#define ICON_CELL_W         60
#define ICON_CELL_H         74
#define ICON_PADDING        6

// ============================================================
// Timing Constants
// ============================================================
#define TOUCH_POLL_MS       20
#define IMU_POLL_MS         20      // 50 Hz
#define BATTERY_POLL_MS     30000   // 30 seconds
#define DISPLAY_TIMEOUT_MS  15000   // sleep after 15s inactivity
#define DIM_TIMEOUT_MS      10000   // dim after 10s inactivity
#define POWER_HOLD_OFF_MS   10000   // long-press power button to hard-off
#define TARGET_FPS          30
#define FRAME_TIME_MS       (1000 / TARGET_FPS)

// ============================================================
// Step Detection
// ============================================================
#define STEP_THRESHOLD      1.2f    // g threshold for step detection
#define STEP_MIN_INTERVAL   250     // ms between steps (debounce)
#define DEFAULT_STEP_GOAL   10000

// ============================================================
// Battery Voltage Thresholds (LiPo via voltage divider)
// ============================================================
#define BATTERY_FULL_V      4.2f
#define BATTERY_NOMINAL_V   3.7f
#define BATTERY_LOW_V       3.4f
#define BATTERY_CRITICAL_V  3.2f
#define BATTERY_EMPTY_V     3.0f
// Voltage divider ratio. Default 3.0 matches the Waveshare RP2040 watch
// (VSYS/3 via internal 200K/100K). Override per-prototype via build_flags
// (Prototype 2 uses 2.0 for a 100K/100K divider from TP4056 B+).
#ifndef BATTERY_DIVIDER
#define BATTERY_DIVIDER     3.0f
#endif

// ============================================================
// Screen Transition
// ============================================================
#define TRANSITION_DURATION_MS  250

// ============================================================
// Gesture Detection
// ============================================================
#define SWIPE_MIN_DISTANCE  30      // px
#define SWIPE_MAX_TIME      500     // ms
#define LONG_PRESS_TIME     600     // ms
#define LONG_PRESS_MAX_MOVE 15      // px

// ============================================================
// App Stack
// ============================================================
#define MAX_APP_STACK       8

// ============================================================
// Event System
// ============================================================
#define EVENT_QUEUE_SIZE    32

// ============================================================
// Timer System
// ============================================================
#define MAX_TIMERS          16

// ============================================================
// NFC
// ============================================================
#define NFC_SCAN_TIMEOUT_MS 200

// ============================================================
// Sound Frequencies (Hz)
// ============================================================
#define SND_CLICK_FREQ      4000
#define SND_CLICK_DUR       10
#define SND_BEEP_FREQ       2000
#define SND_BEEP_DUR        100
#define SND_ALARM_FREQ_LO   1000
#define SND_ALARM_FREQ_HI   1500

// ============================================================
// Brightness
// ============================================================
#define BRIGHTNESS_DEFAULT  50
#define BRIGHTNESS_DIM      20
#define BRIGHTNESS_MIN      10
#define BRIGHTNESS_MAX      100

// ============================================================
// App IDs
// ============================================================
enum AppId : uint8_t {
    APP_WATCHFACE = 0,
    APP_HOME,
    APP_SETTINGS,
    APP_WALLET,
    APP_NFC,
    APP_HEALTH,
    APP_GAME,
    APP_ALARM,
    APP_CHARGING,
    APP_STATS,
    APP_COUNT
};
