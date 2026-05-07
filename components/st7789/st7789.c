#include "st7789.h"

// Hardware pins — fixed for this module (verified from reference driver)
#define _SCLK  3
#define _MOSI  4
#define _RST   7
#define _DC    8
#define _BL    9
#define _SPI   SPI2_HOST

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "st7789";

static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_fb = NULL;   // 240*240 frame buffer, DMA-safe

// ST7789 sends pixels big-endian (high byte first).
// ESP32 is little-endian → must swap each uint16_t before sending.
static inline uint16_t sw16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

// ── Init ──────────────────────────────────────────────────────────────────

esp_err_t st7789_init(void)
{
    // Backlight ON immediately
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << _BL,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(_BL, 1);

    // SPI bus
    spi_bus_config_t bus = {
        .sclk_io_num     = _SCLK,
        .mosi_io_num     = _MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * 8 * sizeof(uint16_t) + 64,
    };
    esp_err_t err = spi_bus_initialize(_SPI, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    // Panel IO
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = _DC,
        .cs_gpio_num       = -1,              // CS tied to GND on module
        .pclk_hz           = 10 * 1000 * 1000,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 3,               // CPOL=1, CPHA=1 — CRITICAL
        .trans_queue_depth = 10,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)_SPI, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel_io_spi: %s", esp_err_to_name(err));
        return err;
    }

    // ST7789 panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = _RST,
        .bits_per_pixel = 16,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
    };
    err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "new_panel_st7789: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true)); // REQUIRED for this module
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // Allocate framebuffer in DMA-capable heap
    s_fb = heap_caps_malloc(LCD_W * LCD_H * sizeof(uint16_t),
                             MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_fb) {
        ESP_LOGE(TAG, "framebuf oom (%d bytes needed)", LCD_W * LCD_H * 2);
        return ESP_ERR_NO_MEM;
    }

    st7789_fill(COLOR_BLACK);
    ESP_LOGI(TAG, "ST7789 ready (240x240, mode3, inverted, fb=%p)", (void*)s_fb);
    return ESP_OK;
}

void st7789_set_backlight(uint8_t pct)
{
    // Simple on/off; PWM dimming can be added later via ledc
    gpio_set_level(_BL, pct > 0 ? 1 : 0);
}

// ── Direct draw ───────────────────────────────────────────────────────────

void st7789_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_panel || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;

    const int CHUNK = 8;
    size_t n = (size_t)w * CHUNK;
    uint16_t *buf = heap_caps_malloc(n * sizeof(uint16_t),
                                     MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) return;
    uint16_t c = sw16(color);
    for (size_t i = 0; i < n; i++) buf[i] = c;

    int left = h, yy = y;
    while (left > 0) {
        int rows = left > CHUNK ? CHUNK : left;
        esp_lcd_panel_draw_bitmap(s_panel, x, yy, x + w, yy + rows, buf);
        yy += rows; left -= rows;
    }
    heap_caps_free(buf);
}

void st7789_fill(uint16_t color)
{
    st7789_fill_rect(0, 0, LCD_W, LCD_H, color);
}

// ── Frame buffer ──────────────────────────────────────────────────────────

uint16_t *st7789_get_fb(void) { return s_fb; }

void st7789_flush(void)
{
    if (!s_panel || !s_fb) return;
    // Push in 8-row stripes (8*240*2 = 3840 B per transaction — safe for SPI DMA)
    const int STRIPE = 8;
    for (int y = 0; y < LCD_H; y += STRIPE) {
        int rows = (y + STRIPE <= LCD_H) ? STRIPE : (LCD_H - y);
        esp_lcd_panel_draw_bitmap(s_panel,
                                  0,       y,
                                  LCD_W,   y + rows,
                                  s_fb + y * LCD_W);
    }
}

// ── FB pixel helpers ──────────────────────────────────────────────────────

void st7789_fb_fill(uint16_t color)
{
    if (!s_fb) return;
    uint16_t c = sw16(color);
    for (int i = 0; i < LCD_W * LCD_H; i++) s_fb[i] = c;
}

void st7789_fb_pixel(int x, int y, uint16_t color)
{
    if (!s_fb || x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    s_fb[y * LCD_W + x] = sw16(color);
}

void st7789_fb_hline(int x, int y, int len, uint16_t color)
{
    if (!s_fb || y < 0 || y >= LCD_H || len <= 0) return;
    if (x < 0) { len += x; x = 0; }
    if (x + len > LCD_W) len = LCD_W - x;
    if (len <= 0) return;
    uint16_t c = sw16(color);
    uint16_t *row = s_fb + y * LCD_W + x;
    for (int i = 0; i < len; i++) row[i] = c;
}

void st7789_fb_vline(int x, int y, int len, uint16_t color)
{
    if (!s_fb || x < 0 || x >= LCD_W || len <= 0) return;
    if (y < 0) { len += y; y = 0; }
    if (y + len > LCD_H) len = LCD_H - y;
    if (len <= 0) return;
    uint16_t c = sw16(color);
    for (int i = 0; i < len; i++) s_fb[(y + i) * LCD_W + x] = c;
}

void st7789_fb_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int dy = 0; dy < h; dy++) st7789_fb_hline(x, y + dy, w, color);
}

void st7789_fb_rect_outline(int x, int y, int w, int h, uint16_t color)
{
    st7789_fb_hline(x, y,         w, color);
    st7789_fb_hline(x, y + h - 1, w, color);
    st7789_fb_vline(x,         y, h, color);
    st7789_fb_vline(x + w - 1, y, h, color);
}

void st7789_fb_circle(int cx, int cy, int r, uint16_t color)
{
    // Bresenham midpoint circle
    int x = 0, y = r, d = 1 - r;
    while (x <= y) {
        st7789_fb_pixel(cx+x, cy+y, color); st7789_fb_pixel(cx-x, cy+y, color);
        st7789_fb_pixel(cx+x, cy-y, color); st7789_fb_pixel(cx-x, cy-y, color);
        st7789_fb_pixel(cx+y, cy+x, color); st7789_fb_pixel(cx-y, cy+x, color);
        st7789_fb_pixel(cx+y, cy-x, color); st7789_fb_pixel(cx-y, cy-x, color);
        if (d < 0) { d += 2*x + 3; }
        else       { d += 2*(x - y) + 5; y--; }
        x++;
    }
}

void st7789_fb_circle_fill(int cx, int cy, int r, uint16_t color)
{
    for (int y = -r; y <= r; y++) {
        int w = (int)sqrtf((float)(r*r - y*y));
        st7789_fb_hline(cx - w, cy + y, 2*w + 1, color);
    }
}
