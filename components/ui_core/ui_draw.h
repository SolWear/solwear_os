#pragma once
#include <stdint.h>
#include "st7789.h"
#include "ui_font.h"

// ============================================================
// High-level drawing helpers on top of st7789 frame buffer
// ============================================================

// Draw a rounded rectangle (corner radius r)
void ui_rounded_rect(int x, int y, int w, int h, int r, uint16_t color);
void ui_rounded_rect_outline(int x, int y, int w, int h, int r, uint16_t color);

// Draw battery icon (x,y = top-left, w=30 h=14)
void ui_battery_icon(int x, int y, uint8_t pct, bool charging);

// Status bar (20px high) — time string + battery
void ui_status_bar(const char *time_str, uint8_t bat_pct, bool charging);

// NFC armed indicator (small icon, top-right area)
void ui_nfc_icon(int x, int y, bool armed);

// Filled triangle
void ui_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);

// Horizontal gradient fill (for watchface backgrounds)
void ui_gradient_h(int x, int y, int w, int h, uint16_t c0, uint16_t c1);

// Draw NFC arm/disarm center overlay widget (auto-dismisses — caller manages timer)
void ui_nfc_widget(bool armed);

// Draw selection highlight box
void ui_select_box(int x, int y, int w, int h, uint16_t color);
