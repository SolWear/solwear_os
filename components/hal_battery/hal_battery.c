#include "hal_battery.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc = NULL;
static uint8_t  s_pct     = 100;
static int      s_mv      = 4200;
static bool     s_charging = false;

// LW 303040 3.7V 350mAh voltage-to-percent table
// (voltage in mV → percent)
static const struct { int mv; uint8_t pct; } s_lut[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70},
    {3800, 60},  {3700, 50}, {3600, 35}, {3500, 20},
    {3400, 10},  {3300, 5},  {3000, 0},
};

void hal_battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    adc_oneshot_new_unit(&unit_cfg, &s_adc);

    // GP2 = ADC1 channel 1 on ESP32-S3
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,  // renamed in IDF 6.x (was DB_11)
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_1, &ch_cfg);
    ESP_LOGI(TAG, "Battery ADC ready (GP2, 100K/100K divider)");
}

void hal_battery_update(void)
{
    if (!s_adc) return;
    int raw = 0;
    adc_oneshot_read(s_adc, ADC_CHANNEL_1, &raw);

    // 12-bit ADC, 3.3V ref, 100K/100K divider → Vbat = Vadc * 2
    int mv_adc = (raw * 3300) / 4095;
    int mv_bat = mv_adc * 2;  // divider factor

    // Clamp
    if (mv_bat > 4200) mv_bat = 4200;
    if (mv_bat < 3000) mv_bat = 3000;
    s_mv = mv_bat;

    // Detect charging: voltage rising or above 4.1V with uncertainty
    s_charging = (mv_bat > 4100);

    // LUT interpolation
    int n = sizeof(s_lut) / sizeof(s_lut[0]);
    if (mv_bat >= s_lut[0].mv) {
        s_pct = 100;
    } else if (mv_bat <= s_lut[n-1].mv) {
        s_pct = 0;
    } else {
        for (int i = 0; i < n - 1; i++) {
            if (mv_bat <= s_lut[i].mv && mv_bat >= s_lut[i+1].mv) {
                int range  = s_lut[i].mv - s_lut[i+1].mv;
                int offset = mv_bat - s_lut[i+1].mv;
                s_pct = s_lut[i+1].pct + (uint8_t)((s_lut[i].pct - s_lut[i+1].pct) * offset / range);
                break;
            }
        }
    }
}

uint8_t hal_battery_percent(void)  { return s_pct; }
int     hal_battery_mv(void)       { return s_mv; }
bool    hal_battery_charging(void) { return s_charging; }
bool    hal_battery_low(void)      { return s_pct <= 15; }
bool    hal_battery_critical(void) { return s_pct <= 5; }
