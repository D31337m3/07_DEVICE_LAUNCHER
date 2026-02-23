#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdcard_service_mount(void);
bool sdcard_service_is_mounted(void);
const char *sdcard_service_mount_point(void);
esp_err_t sdcard_service_last_error(void);

// Human-readable status of the most recent mount attempt (for UI/debug).
// Returns a pointer to an internal static buffer.
const char *sdcard_service_last_status(void);

#ifdef __cplusplus
}
#endif
