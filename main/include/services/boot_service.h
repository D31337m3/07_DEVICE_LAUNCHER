#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool first_boot_after_flash;
} boot_service_result_t;

// Initializes all hardware/services needed for UI and apps.
// Order:
// - NVS
// - I2C
// - Display + LVGL
// - Audio
// - Boot splash (logo + spray-rattle audio) for 8 seconds
// - Remaining services (power/time/imu/ble/wifi/app manager/power manager)
esp_err_t boot_service_init(boot_service_result_t *out_result);

#ifdef __cplusplus
}
#endif
