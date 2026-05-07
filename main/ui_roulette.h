#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hal_buttons.h"

// ============================================================
// Roulette spinner widget
// Charset: digits 1-8, then A-Z, then OK sentinel (ALPHA mode)
//          digits 1-8 only, then OK (PIN mode)
//          hex 0-9 A-F, then OK (HEX mode)
//
// K1 = scroll up (prev char)
// K2 = scroll down (next char)
// K3 = confirm current char (or submit if on OK and len >= min_len)
// K4 = delete last char (returns ROULETTE_CANCELLED if buf empty)
// ============================================================

typedef enum {
    ROULETTE_MODE_PIN,    // 1-8 only + OK
    ROULETTE_MODE_ALPHA,  // 1-8, A-Z + OK
    ROULETTE_MODE_HEX,    // 0-9, A-F + OK
} roulette_mode_t;

typedef enum {
    ROULETTE_ACTIVE,
    ROULETTE_DONE,
    ROULETTE_CANCELLED,
} roulette_result_t;

typedef struct {
    roulette_mode_t mode;
    uint8_t  max_len;    // max chars
    uint8_t  min_len;    // min before OK allowed
    bool     secret;     // show dots instead of chars
    uint8_t  cursor;     // current charset index
    char     buf[33];    // entered string + null
    uint8_t  len;        // current length
    int16_t  y0;         // top-y of widget in framebuf
} roulette_t;

void           roulette_init(roulette_t *r, roulette_mode_t mode,
                             uint8_t max_len, uint8_t min_len,
                             bool secret, int16_t y0);
void           roulette_reset(roulette_t *r);
roulette_result_t roulette_feed(roulette_t *r, btn_event_t ev);
void           roulette_render(const roulette_t *r);

// Total chars in charset for this mode (including OK sentinel)
uint8_t        roulette_charset_size(roulette_mode_t mode);
char           roulette_char_at(roulette_mode_t mode, uint8_t idx);
