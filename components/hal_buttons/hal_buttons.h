#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_NONE = 0,
    BTN_K1_PRESS,
    BTN_K2_PRESS,
    BTN_K3_PRESS,
    BTN_K4_PRESS,
    BTN_K1_DOUBLE,   // quick double-press K1
    BTN_K4_DOUBLE,   // double-press K4 -> go home
    BTN_K4_TRIPLE,   // legacy triple-press K4
    BTN_K1_HOLD,     // hold K1 for 5 seconds -> NFC arm/disarm
    BTN_K4_HOLD,     // hold K4 for 5 seconds -> lock device
} btn_event_t;

typedef void (*btn_callback_t)(btn_event_t ev);

void        hal_buttons_init(btn_callback_t cb);
btn_event_t hal_buttons_poll(void);
