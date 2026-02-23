#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_lvgl_init(void);

bool display_lvgl_lock(int timeout_ms);
void display_lvgl_unlock(void);

void display_lvgl_set_on(bool on);
void display_lvgl_set_brightness(uint8_t brightness_percent);
uint8_t display_lvgl_get_brightness(void);

#ifdef __cplusplus
}
#endif
