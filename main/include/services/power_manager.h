#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the background power manager (idle display off + optional sleep).
// Safe to call multiple times.
esp_err_t power_manager_init(void);

void power_manager_set_idle_timeout_sec(uint32_t sec);
uint32_t power_manager_get_idle_timeout_sec(void);

void power_manager_set_sleep_timeout_sec(uint32_t sec);
uint32_t power_manager_get_sleep_timeout_sec(void);

// Immediate actions
void power_manager_reboot_now(void);
void power_manager_sleep_now(void);
void power_manager_shutdown_now(void);

#ifdef __cplusplus
}
#endif
