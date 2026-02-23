#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mount internal flash FATFS partition "storage" at /storage.
// This partition is defined in partitions.csv.

esp_err_t storage_service_mount(void);
bool storage_service_is_mounted(void);
const char *storage_service_mount_point(void);
esp_err_t storage_service_last_error(void);

#ifdef __cplusplus
}
#endif
