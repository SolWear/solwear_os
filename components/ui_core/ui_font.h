#pragma once
#include <stdint.h>
#include "st7789.h"

// ============================================================
// Bitmap font rendering — 6x8 built-in font, scale 1x–4x
// Uses the frame buffer (st7789_fb_*) for drawing
// ============================================================

// Draw a single character at (x,y), returns next x
int ui_char(int x, int y, char c, uint16_t color, uint8_t scale);

// Draw a null-terminated string, returns end x
int ui_str(int x, int y, const char *s, uint16_t color, uint8_t scale);

// Centered string on LCD_W
void ui_str_center(int y, const char *s, uint16_t color, uint8_t scale);

// Right-aligned string ending at x_end
void ui_str_right(int x_end, int y, const char *s, uint16_t color, uint8_t scale);

// String pixel width
int ui_str_width(const char *s, uint8_t scale);

// Large digit (for clock) — uses 2x font size glyphs
void ui_large_time(int x, int y, const char *hhmm, uint16_t color);
