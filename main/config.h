#pragma once
#include <stdint.h>

// ============================================================
// SolWearOS — Master Configuration
// Target: ESP32-S3 Mini + ST7789 240x240 IPS
// ============================================================

// --- Display (ST7789 via SPI) ---
#define PIN_LCD_SCLK    3
#define PIN_LCD_MOSI    4
#define PIN_LCD_RST     7
#define PIN_LCD_DC      8
#define PIN_LCD_BL      9
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_FREQ_HZ (40 * 1000 * 1000)
#define LCD_W           240
#define LCD_H           240

// --- Buttons (active LOW, INPUT_PULLUP) ---
#define PIN_BTN_K1      13   // up / previous
#define PIN_BTN_K2      12   // down / next
#define PIN_BTN_K3      11   // confirm / select
#define PIN_BTN_K4      10   // back / cancel

// --- PN532 NFC (I2C) ---
#define PIN_NFC_SDA     5
#define PIN_NFC_SCL     6
#define NFC_I2C_PORT    I2C_NUM_0
#define NFC_I2C_FREQ    100000
#define PN532_I2C_ADDR  0x24

// --- Battery ADC ---
#define PIN_BAT_ADC     2
#define BAT_FULL_MV     4200
#define BAT_EMPTY_MV    3000
#define BAT_CAPACITY_MAH 350

// --- UI Layout ---
#define STATUS_BAR_H    20
#define APP_AREA_Y      STATUS_BAR_H
#define APP_AREA_H      (LCD_H - STATUS_BAR_H)

// --- Timing ---
#define TARGET_FPS          30
#define FRAME_MS            (1000 / TARGET_FPS)
#define BTN_DEBOUNCE_MS     20
#define BTN_DOUBLE_MS       350
#define BTN_TRIPLE_MS       600
#define DISPLAY_SLEEP_MS    15000
#define TX_CONFIRM_TIMER_MS 10000

// --- Wallet storage ---
#define WALLET_NVS_NS       "wallet"
#define WALLET_KEY_SALT     "salt"
#define WALLET_KEY_IV       "iv"
#define WALLET_KEY_SEED_ENC "seed_enc"
#define WALLET_KEY_PUBKEY   "pubkey"
#define WALLET_KEY_NAME     "name"
#define WALLET_NAME_MAX     16
#define PBKDF2_ITER         2000

// --- Receipts (SPIFFS) ---
#define RECEIPTS_PATH       "/spiffs/receipts.json"
#define TAMA_PATH           "/spiffs/tama.bin"
#define TETRIS_HI_PATH      "/spiffs/tetris_hi.bin"
