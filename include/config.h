#pragma once

// ============================================================
// SolWearOS Master Configuration
// Waveshare RP2040-Touch-LCD-1.69
// ============================================================

// --- Display (ST7789V2 via SPI1) ---
#define PIN_LCD_DC      8
#define PIN_LCD_CS      9
#define PIN_LCD_CLK     10
#define PIN_LCD_MOSI    11
#define PIN_LCD_MISO    12
#define PIN_LCD_RST     13
#define PIN_LCD_BL      25

#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   280

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
#define PIN_NFC_SDA     16
#define PIN_NFC_SCL     17
#define NFC_I2C_ADDR    0x24

// --- Battery (ADC) ---
#define PIN_BATTERY_ADC 29
#define BATTERY_ADC_CH  3

// --- Buzzer (PWM) ---
#define PIN_BUZZER      20

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
#define BATTERY_DIVIDER     2.0f    // voltage divider ratio

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
#define BRIGHTNESS_DEFAULT  80
#define BRIGHTNESS_DIM      40
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
