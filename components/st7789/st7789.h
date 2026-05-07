#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ============================================================
// ST7789 driver — 1.3" IPS 240x240, ESP-IDF esp_lcd panel API
//
// Pinout (hardware-fixed for this module):
//   SCL → GPIO 3    SDA → GPIO 4
//   RES → GPIO 7    DC  → GPIO 8
//   BLK → GPIO 9    CS  → GND (tied on PCB, set CS=-1)
//
// Critical quirks:
//   SPI mode 3 (CPOL=1 CPHA=1) — mode 0 = garbage pixels
//   invert_color(true)          — without INVON all colors inverted
//   DMA buffers via MALLOC_CAP_DMA — stack buffers cause corruption
//   Byte-swap RGB565 before sending (little-endian CPU, big-endian wire)
// ============================================================

// RGB565 color helpers
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
#define RGB565(r,g,b) rgb565(r,g,b)

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_ORANGE  0xFD20
#define COLOR_LTGRAY  0x7BEF
#define COLOR_GRAY    0x4208
#define COLOR_DARKBG  rgb565(0x10,0x10,0x18)
#define COLOR_SOL_PUR rgb565(0x99,0x45,0xFF)
#define COLOR_SOL_GRN rgb565(0x14,0xF1,0x95)

#define LCD_W 240
#define LCD_H 240
#define STATUS_BAR_H 20

// Init — backlight, SPI bus, panel IO, ST7789 panel, invert, clear
esp_err_t st7789_init(void);

// Backlight 0-100%
void st7789_set_backlight(uint8_t pct);

// ── Direct draw (uses DMA, immediate, no frame buffer) ──────────────
void st7789_fill(uint16_t color);
void st7789_fill_rect(int x, int y, int w, int h, uint16_t color);

// ── Frame buffer (115 KB in DMA heap) ──────────────────────────────
// get_fb()  : pointer to 240x240 uint16_t array (row-major, RGB565 swapped)
// flush()   : push entire framebuffer to display in 8-row stripes
uint16_t *st7789_get_fb(void);
void      st7789_flush(void);

// Framebuffer drawing primitives (write to fb, call flush() when done)
void st7789_fb_fill(uint16_t color);
void st7789_fb_pixel(int x, int y, uint16_t color);
void st7789_fb_hline(int x, int y, int len, uint16_t color);
void st7789_fb_vline(int x, int y, int len, uint16_t color);
void st7789_fb_rect(int x, int y, int w, int h, uint16_t color);
void st7789_fb_rect_outline(int x, int y, int w, int h, uint16_t color);
void st7789_fb_circle(int cx, int cy, int r, uint16_t color);
void st7789_fb_circle_fill(int cx, int cy, int r, uint16_t color);
