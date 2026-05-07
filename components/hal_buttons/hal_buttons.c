#include "hal_buttons.h"
#include "driver/gpio.h"  // esp_driver_gpio in IDF 6.x
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PIN_K1  13
#define PIN_K2  12
#define PIN_K3  11
#define PIN_K4  10

#define DEBOUNCE_MS  20
#define DOUBLE_MS    350
#define TRIPLE_MS    600

static btn_callback_t s_cb = NULL;

// Per-button state
typedef struct {
    bool     last_raw;
    bool     pressed;        // debounced state
    uint32_t press_time_ms;  // when it was pressed
} btn_state_t;

static btn_state_t s_btn[4];  // 0=K1 1=K2 2=K3 3=K4

// Multi-press counters for K1 and K4
static uint8_t  s_k1_count = 0;
static uint32_t s_k1_last_ms = 0;
static uint8_t  s_k4_count = 0;
static uint32_t s_k4_last_ms = 0;

static volatile btn_event_t s_pending = BTN_NONE;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void process_press(int idx, uint32_t t)
{
    btn_event_t ev = BTN_NONE;

    switch (idx) {
        case 0:  // K1
            if (t - s_k1_last_ms < DOUBLE_MS) {
                s_k1_count++;
            } else {
                s_k1_count = 1;
            }
            s_k1_last_ms = t;
            if (s_k1_count >= 2) {
                ev = BTN_K1_DOUBLE;
                s_k1_count = 0;
            } else {
                ev = BTN_K1_PRESS;
            }
            break;
        case 1:  // K2
            ev = BTN_K2_PRESS;
            break;
        case 2:  // K3
            ev = BTN_K3_PRESS;
            break;
        case 3:  // K4
            if (t - s_k4_last_ms < TRIPLE_MS) {
                s_k4_count++;
            } else {
                s_k4_count = 1;
            }
            s_k4_last_ms = t;
            if (s_k4_count >= 3) {
                ev = BTN_K4_TRIPLE;
                s_k4_count = 0;
            } else if (s_k4_count >= 2) {
                ev = BTN_K4_DOUBLE;
                // Don't reset — allow triple on next press
            } else {
                ev = BTN_K4_PRESS;
            }
            break;
    }

    if (ev != BTN_NONE) {
        s_pending = ev;
        if (s_cb) s_cb(ev);
    }
}

static void btn_poll_task(void *arg)
{
    const int pins[4] = {PIN_K1, PIN_K2, PIN_K3, PIN_K4};

    while (1) {
        uint32_t t = now_ms();
        for (int i = 0; i < 4; i++) {
            bool raw = (gpio_get_level(pins[i]) == 0);
            if (raw && !s_btn[i].last_raw) {
                // Falling edge (press)
                s_btn[i].press_time_ms = t;
            } else if (!raw && s_btn[i].last_raw) {
                // Rising edge (release) — confirm press after debounce
                if (t - s_btn[i].press_time_ms >= DEBOUNCE_MS) {
                    process_press(i, t);
                }
            }
            s_btn[i].last_raw = raw;
        }
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
    }
}

void hal_buttons_init(btn_callback_t cb)
{
    s_cb = cb;
    memset(s_btn, 0, sizeof(s_btn));

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_K1) | (1ULL << PIN_K2) |
                        (1ULL << PIN_K3) | (1ULL << PIN_K4),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    xTaskCreate(btn_poll_task, "buttons", 2048, NULL, 5, NULL);
}

btn_event_t hal_buttons_poll(void)
{
    btn_event_t ev = s_pending;
    s_pending = BTN_NONE;
    return ev;
}
