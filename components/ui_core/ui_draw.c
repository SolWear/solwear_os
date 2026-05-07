#include "ui_draw.h"
#include "st7789.h"
#include "ui_font.h"
#include <math.h>
#include <stdio.h>

void ui_rounded_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    if (r < 1) { st7789_fb_rect(x, y, w, h, color); return; }
    // Fill center strip
    st7789_fb_rect(x + r, y,     w - 2*r, h,   color);
    st7789_fb_rect(x,     y + r, r,       h - 2*r, color);
    st7789_fb_rect(x+w-r, y + r, r,       h - 2*r, color);
    // Corners via quarter circles
    for (int dy = 0; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r*r - dy*dy));
        // top-left
        st7789_fb_hline(x + r - dx, y + r - dy, dx, color);
        // top-right
        st7789_fb_hline(x + w - r,  y + r - dy, dx, color);
        // bottom-left
        st7789_fb_hline(x + r - dx, y + h - r + dy - 1, dx, color);
        // bottom-right
        st7789_fb_hline(x + w - r,  y + h - r + dy - 1, dx, color);
    }
}

void ui_rounded_rect_outline(int x, int y, int w, int h, int r, uint16_t color)
{
    if (r < 1) { st7789_fb_rect_outline(x, y, w, h, color); return; }
    st7789_fb_hline(x + r, y,         w - 2*r, color);
    st7789_fb_hline(x + r, y + h - 1, w - 2*r, color);
    st7789_fb_vline(x,         y + r, h - 2*r, color);
    st7789_fb_vline(x + w - 1, y + r, h - 2*r, color);
    // Quarter-circle corners (outline only via Bresenham)
    int px = 0, py = r, d = 1 - r;
    while (px <= py) {
        st7789_fb_pixel(x+r-px, y+r-py, color);
        st7789_fb_pixel(x+w-r+px-1, y+r-py, color);
        st7789_fb_pixel(x+r-px, y+h-r+py-1, color);
        st7789_fb_pixel(x+w-r+px-1, y+h-r+py-1, color);
        st7789_fb_pixel(x+r-py, y+r-px, color);
        st7789_fb_pixel(x+w-r+py-1, y+r-px, color);
        st7789_fb_pixel(x+r-py, y+h-r+px-1, color);
        st7789_fb_pixel(x+w-r+py-1, y+h-r+px-1, color);
        if (d < 0) d += 2*px + 3;
        else { d += 2*(px-py) + 5; py--; }
        px++;
    }
}

void ui_battery_icon(int x, int y, uint8_t pct, bool charging)
{
    // Body: 24x12, tip: 3x6
    const int BW = 24, BH = 12;
    st7789_fb_rect_outline(x, y, BW, BH, COLOR_LTGRAY);
    // Tip
    st7789_fb_rect(x + BW, y + 3, 3, 6, COLOR_LTGRAY);

    // Fill level
    uint16_t fill_color = (pct > 30) ? COLOR_SOL_GRN :
                          (pct > 15) ? COLOR_YELLOW   : COLOR_RED;
    if (charging) fill_color = COLOR_CYAN;
    int fill_w = (int)((BW - 4) * pct / 100);
    if (fill_w > BW - 4) fill_w = BW - 4;
    if (fill_w > 0) {
        st7789_fb_rect(x + 2, y + 2, fill_w, BH - 4, fill_color);
    }

    if (charging) {
        // Tiny lightning bolt glyph
        st7789_fb_pixel(x + BW/2,     y + 2, COLOR_WHITE);
        st7789_fb_pixel(x + BW/2 - 1, y + 5, COLOR_WHITE);
        st7789_fb_pixel(x + BW/2,     y + 5, COLOR_WHITE);
        st7789_fb_pixel(x + BW/2 - 1, y + 9, COLOR_WHITE);
    }
}

void ui_status_bar(const char *time_str, uint8_t bat_pct, bool charging)
{
    // Dark background strip
    st7789_fb_rect(0, 0, LCD_W, STATUS_BAR_H, COLOR_DARKBG);

    // Time (left side, scale 2)
    ui_str(4, (STATUS_BAR_H - 8*2) / 2, time_str, COLOR_WHITE, 2);

    // Battery (right side)
    ui_battery_icon(LCD_W - 30, (STATUS_BAR_H - 12) / 2, bat_pct, charging);
}

void ui_nfc_icon(int x, int y, bool armed)
{
    uint16_t c = armed ? COLOR_SOL_GRN : COLOR_RED;
    st7789_fb_circle(x + 6, y + 6, 6, c);
    st7789_fb_circle(x + 6, y + 6, 4, c);
    st7789_fb_pixel(x + 6, y + 6, c);
    if (!armed) {
        // Strikethrough
        st7789_fb_hline(x, y + 6, 14, COLOR_RED);
    }
}

void ui_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    // Sort by y
    if (y1 < y0) { int t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    if (y2 < y0) { int t; t=x0;x0=x2;x2=t; t=y0;y0=y2;y2=t; }
    if (y2 < y1) { int t; t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }
    if (y0 == y2) return;

    for (int y = y0; y <= y2; y++) {
        int xa, xb;
        if (y < y1) {
            xa = x0 + (x1 - x0) * (y - y0) / (y1 - y0 + 1);
            xb = x0 + (x2 - x0) * (y - y0) / (y2 - y0 + 1);
        } else {
            xa = x1 + (x2 - x1) * (y - y1) / (y2 - y1 + 1);
            xb = x0 + (x2 - x0) * (y - y0) / (y2 - y0 + 1);
        }
        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        st7789_fb_hline(xa, y, xb - xa + 1, color);
    }
}

void ui_gradient_h(int x, int y, int w, int h, uint16_t c0, uint16_t c1)
{
    uint8_t r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;

    for (int i = 0; i < h; i++) {
        uint8_t r = r0 + (r1 - r0) * i / h;
        uint8_t g = g0 + (g1 - g0) * i / h;
        uint8_t b = b0 + (b1 - b0) * i / h;
        uint16_t c = (r << 11) | (g << 5) | b;
        st7789_fb_hline(x, y + i, w, c);
    }
}

void ui_nfc_widget(bool armed)
{
    // 90x72 centered overlay
    int wx = (LCD_W - 90) / 2;
    int wy = (LCD_H - 72) / 2;
    ui_rounded_rect(wx, wy, 90, 72, 10, COLOR_DARKBG);
    uint16_t c = armed ? COLOR_SOL_GRN : COLOR_RED;
    ui_rounded_rect_outline(wx, wy, 90, 72, 10, c);

    // NFC rings
    int cx = LCD_W / 2, cy = LCD_H / 2 - 4;
    st7789_fb_circle(cx, cy, 6,  c);
    st7789_fb_circle(cx, cy, 12, c);
    st7789_fb_circle(cx, cy, 18, c);
    st7789_fb_circle_fill(cx, cy, 3, c);

    if (!armed) {
        // Diagonal strikethrough
        for (int i = -2; i <= 2; i++) {
            for (int k = 0; k < 40; k++) {
                st7789_fb_pixel(wx + 5 + k, wy + 5 + k + i, COLOR_RED);
            }
        }
    }

    const char *label = armed ? "NFC ON" : "NFC OFF";
    ui_str_center(wy + 72 - 14, label, c, 1);
}

void ui_select_box(int x, int y, int w, int h, uint16_t color)
{
    ui_rounded_rect_outline(x - 2, y - 2, w + 4, h + 4, 6, color);
    ui_rounded_rect_outline(x - 3, y - 3, w + 6, h + 6, 7, color);
}
