#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_terminal_init(void);

// Open terminal if already initialized; otherwise initializes and opens it.
esp_err_t ui_terminal_open(void);

#ifdef __cplusplus
}
#endif
