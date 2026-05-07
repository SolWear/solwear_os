#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_NONE = 0,
    BTN_K1_PRESS,
    BTN_K2_PRESS,
    BTN_K3_PRESS,
    BTN_K4_PRESS,
    BTN_K1_DOUBLE,   // double-press K1 → NFC arm/disarm
    BTN_K4_DOUBLE,   // double-press K4 → go home
    BTN_K4_TRIPLE,   // triple-press K4 → lock device
} btn_event_t;

typedef void (*btn_callback_t)(btn_event_t ev);

void        hal_buttons_init(btn_callback_t cb);
btn_event_t hal_buttons_poll(void);
