#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_service_init(void);
esp_err_t ble_service_set_enabled(bool enabled);
bool ble_service_is_enabled(void);

#ifdef __cplusplus
}
#endif
