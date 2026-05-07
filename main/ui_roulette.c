#include "ui_roulette.h"
#include "st7789.h"
#include "ui_draw.h"
#include "ui_font.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

#define OK_SENTINEL '\x01'
#define ROW_H       34

uint8_t roulette_charset_size(roulette_mode_t mode)
{
    switch (mode) {
        case ROULETTE_MODE_PIN:   return 9;  // 1-8 + OK
        case ROULETTE_MODE_ALPHA: return 35; // 1-8, A-Z + OK
        case ROULETTE_MODE_HEX:  return 17; // 0-9, A-F + OK
    }
    return 9;
}

char roulette_char_at(roulette_mode_t mode, uint8_t idx)
{
    switch (mode) {
        case ROULETTE_MODE_PIN:
            if (idx < 8) return '1' + idx;
            return OK_SENTINEL;

        case ROULETTE_MODE_ALPHA:
            if (idx < 8)  return '1' + idx;
            if (idx < 34) return 'A' + (idx - 8);
            return OK_SENTINEL;

        case ROULETTE_MODE_HEX:
            if (idx < 10) return '0' + idx;
            if (idx < 16) return 'A' + (idx - 10);
            return OK_SENTINEL;
    }
    return '?';
}

void roulette_init(roulette_t *r, roulette_mode_t mode,
                   uint8_t max_len, uint8_t min_len,
                   bool secret, int16_t y0)
{
    r->mode    = mode;
    r->max_len = max_len;
    r->min_len = min_len;
    r->secret  = secret;
    r->cursor  = 0;
    r->len     = 0;
    r->y0      = y0;
    memset(r->buf, 0, sizeof(r->buf));
}

void roulette_reset(roulette_t *r)
{
    r->cursor = 0;
    r->len    = 0;
    memset(r->buf, 0, sizeof(r->buf));
}

roulette_result_t roulette_feed(roulette_t *r, btn_event_t ev)
{
    uint8_t total = roulette_charset_size(r->mode);

    if (ev == BTN_K1_PRESS) {
        // Scroll up (prev char)
        r->cursor = (r->cursor == 0) ? total - 1 : r->cursor - 1;

    } else if (ev == BTN_K2_PRESS) {
        // Scroll down (next char)
        r->cursor = (r->cursor + 1) % total;

    } else if (ev == BTN_K3_PRESS) {
        // Confirm current char or submit
        char ch = roulette_char_at(r->mode, r->cursor);
        if (ch == OK_SENTINEL) {
            if (r->len >= r->min_len) return ROULETTE_DONE;
            // Not enough chars yet — ignore
        } else if (r->len < r->max_len) {
            r->buf[r->len++] = ch;
            r->buf[r->len]   = '\0';
            // Auto-submit when full
            if (r->len >= r->max_len && r->len >= r->min_len) {
                return ROULETTE_DONE;
            }
        }

    } else if (ev == BTN_K4_PRESS) {
        // Delete last char
        if (r->len > 0) {
            r->len--;
            r->buf[r->len] = '\0';
        } else {
            return ROULETTE_CANCELLED;
        }
    }

    return ROULETTE_ACTIVE;
}

void roulette_render(const roulette_t *r)
{
    uint8_t total = roulette_charset_size(r->mode);
    int cx = LCD_W / 2;
    int drum_y = r->y0 + 4;

    // Drum background
    ui_rounded_rect(cx - 44, drum_y, 88, ROW_H * 3, 8, COLOR_DARKBG);

    // Draw 3 rows: prev, current, next
    for (int8_t row = -1; row <= 1; row++) {
        uint8_t idx = (uint8_t)((r->cursor + total + row) % total);
        char ch  = roulette_char_at(r->mode, idx);

        const char *str;
        char sbuf[3] = {ch, '\0', '\0'};
        if (ch == OK_SENTINEL) str = "OK";
        else                    str = sbuf;

        int row_y = drum_y + (row + 1) * ROW_H + (ROW_H - 16) / 2;
        uint8_t scale = (row == 0) ? 3 : 2;
        uint16_t color;
        if (row == 0) {
            color = (ch == OK_SENTINEL) ? COLOR_SOL_GRN : COLOR_WHITE;
        } else {
            color = COLOR_GRAY;
        }

        int tw = ui_str_width(str, scale);
        ui_str(cx - tw / 2, row_y, str, color, scale);
    }

    // Highlight box around current row
    ui_rounded_rect_outline(cx - 42, drum_y + ROW_H, 84, ROW_H, 6, COLOR_SOL_PUR);

    // Buffer display (dots for secret, chars for text)
    int buf_y = drum_y + ROW_H * 3 + 10;
    char disp[33] = {};
    if (r->secret) {
        for (uint8_t i = 0; i < r->len; i++) disp[i] = '*';
        disp[r->len] = '\0';
    } else {
        memcpy(disp, r->buf, r->len + 1);
    }

    const char *show = (r->len == 0) ? "_" : disp;
    int tw = ui_str_width(show, 2);
    ui_str(cx - tw / 2, buf_y, show, COLOR_SOL_GRN, 2);

    // Char/max counter
    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%u/%u", r->len, r->max_len);
    int ctw = ui_str_width(cnt, 1);
    ui_str(cx - ctw / 2, buf_y + 18, cnt, COLOR_GRAY, 1);

    // Hint
    ui_str_center(buf_y + 32, "K1^ K2v K3=OK K4=DEL", COLOR_GRAY, 1);
}
