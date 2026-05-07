#pragma once
#include <stdint.h>
#include <stdbool.h>

void    hal_battery_init(void);
void    hal_battery_update(void);
uint8_t hal_battery_percent(void);
int     hal_battery_mv(void);
bool    hal_battery_charging(void);
bool    hal_battery_low(void);       // < 15%
bool    hal_battery_critical(void);  // < 5%
