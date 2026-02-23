#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Monitors VBUS (USB/PC power) and triggers on-connect behaviors.
esp_err_t pc_connect_service_init(void);

#ifdef __cplusplus
}
#endif
