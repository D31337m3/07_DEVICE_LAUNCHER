#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_launcher_init(void);

// Override the screen that the generic "Back" button returns to.
// Useful when opening Settings from the carousel.
void ui_launcher_set_home_screen(lv_obj_t *home_screen);

// Open the Settings menu screen directly.
esp_err_t ui_launcher_open_settings(void);

// Called from a 1Hz LVGL timer to refresh the status bar.
void ui_launcher_status_update(void);

#ifdef __cplusplus
}
#endif
