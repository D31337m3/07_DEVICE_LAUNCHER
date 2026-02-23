#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool power_axp2101_init(i2c_master_bus_handle_t bus);
int power_axp2101_get_battery_percent(void);
int power_axp2101_get_batt_mv(void);
bool power_axp2101_is_charging(void);
bool power_axp2101_is_vbus_present(void);

#ifdef __cplusplus
}
#endif
