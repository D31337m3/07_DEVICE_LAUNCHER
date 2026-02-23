#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "display_lvgl.h"
#include "ui_launcher.h"
#include "ui_app_carousel.h"

#include "services/boot_service.h"


static const char *TAG = "app";
#include "wifi_manager.h"
#include "radio_player.h"

extern "C" void app_main(void)
{
    boot_service_result_t boot = {};
    esp_err_t boot_err = boot_service_init(&boot);
    if (boot_err != ESP_OK) {
        ESP_LOGE(TAG, "boot_service_init failed: %s", esp_err_to_name(boot_err));
        ESP_LOGE(TAG, "Halting to avoid reboot loop; check earlier logs for the root cause.");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Initialize WiFi manager and web UI (after boot_service so netif/event loop are ready)
    wifi_manager_init();

    // Initialize radio player
    radio_player_init();

    ESP_LOGI(TAG, "Init UI - App Carousel");
    ESP_LOGI(TAG, "About to lock display...");
    if (display_lvgl_lock(-1)) {
        ESP_LOGI(TAG, "Display locked, calling carousel init...");
        esp_err_t err = ui_app_carousel_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CAROUSEL INIT FAILED: %d", err);
            ESP_LOGE(TAG, "Falling back to old launcher...");
            ui_launcher_init();
        } else {
            ESP_LOGI(TAG, "CAROUSEL INIT SUCCESS!");
        }
        display_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to lock display for carousel init");
        ESP_LOGE(TAG, "Loading old launcher as fallback");
        ui_launcher_init();
    }
    
    ESP_LOGI(TAG, "UI init complete");
}

