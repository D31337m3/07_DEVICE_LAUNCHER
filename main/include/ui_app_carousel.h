#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the app carousel screen
esp_err_t ui_app_carousel_init(void);

// Get the carousel screen object
lv_obj_t* ui_app_carousel_get_screen(void);

// Open settings as an app (callback for built-in Settings app)
void ui_app_carousel_open_settings(lv_event_t *e);

#ifdef __cplusplus
}
#endif
