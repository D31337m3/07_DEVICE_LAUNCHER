#include "services/power_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include "nvs.h"

#include "lvgl.h"

#include "display_lvgl.h"
#include "app_pins.h"

static const char *TAG = "pwr_mgr";

static bool s_started = false;
static TaskHandle_t s_task = nullptr;

static uint32_t s_idle_timeout_sec = 30;
static uint32_t s_sleep_timeout_sec = 0; // 0 = never
static bool s_display_off = false;

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("pwr", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint32_t v = 0;
    if (nvs_get_u32(h, "idle_s", &v) == ESP_OK) {
        s_idle_timeout_sec = v;
    }
    if (nvs_get_u32(h, "sleep_s", &v) == ESP_OK) {
        s_sleep_timeout_sec = v;
    }
    nvs_close(h);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open("pwr", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u32(h, "idle_s", s_idle_timeout_sec);
    nvs_set_u32(h, "sleep_s", s_sleep_timeout_sec);
    nvs_commit(h);
    nvs_close(h);
}

static void ensure_wakeup_sources(void)
{
    // Wake on touch interrupt if available.
    if (APP_PIN_NUM_TOUCH_INT != (gpio_num_t)-1) {
        // FT5x06 INT is typically active-low.
        esp_sleep_enable_ext0_wakeup(APP_PIN_NUM_TOUCH_INT, 0);
    }
}

static void power_mgr_task(void *)
{
    ESP_LOGI(TAG, "Power manager task started (idle=%lus sleep=%lus)", (unsigned long)s_idle_timeout_sec,
             (unsigned long)s_sleep_timeout_sec);

    while (true) {
        // LVGL is not thread-safe; guard any LVGL calls.
        uint32_t inactive_ms = 0;
        if (display_lvgl_lock(10)) {
            inactive_ms = lv_disp_get_inactive_time(NULL);
            display_lvgl_unlock();
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const bool should_display_off = (s_idle_timeout_sec > 0) && (inactive_ms >= (s_idle_timeout_sec * 1000U));
        if (should_display_off && !s_display_off) {
            display_lvgl_set_on(false);
            s_display_off = true;
        } else if (!should_display_off && s_display_off) {
            display_lvgl_set_on(true);
            s_display_off = false;
        }

        if (s_sleep_timeout_sec > 0 && inactive_ms >= (s_sleep_timeout_sec * 1000U)) {
            ESP_LOGI(TAG, "Sleep timeout reached - entering sleep");
            power_manager_sleep_now();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t power_manager_init(void)
{
    if (s_started) {
        return ESP_OK;
    }

    nvs_load();

    s_started = true;
    xTaskCreate(power_mgr_task, "pwr_mgr", 4096, NULL, 2, &s_task);
    return ESP_OK;
}

void power_manager_set_idle_timeout_sec(uint32_t sec)
{
    s_idle_timeout_sec = sec;
    nvs_save();
}

uint32_t power_manager_get_idle_timeout_sec(void)
{
    return s_idle_timeout_sec;
}

void power_manager_set_sleep_timeout_sec(uint32_t sec)
{
    s_sleep_timeout_sec = sec;
    nvs_save();
}

uint32_t power_manager_get_sleep_timeout_sec(void)
{
    return s_sleep_timeout_sec;
}

void power_manager_reboot_now(void)
{
    esp_restart();
}

void power_manager_sleep_now(void)
{
    display_lvgl_set_on(false);
    ensure_wakeup_sources();
    esp_light_sleep_start();
    display_lvgl_set_on(true);
}

void power_manager_shutdown_now(void)
{
    display_lvgl_set_on(false);
    ensure_wakeup_sources();
    esp_deep_sleep_start();
}
