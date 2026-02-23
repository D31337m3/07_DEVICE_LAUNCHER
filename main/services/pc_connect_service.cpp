#include "services/pc_connect_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lvgl.h"

#include "services/power_axp2101.h"
#include "services/time_service.h"

#include "ui_terminal.h"

static const char *TAG = "pc_conn";

static bool s_started = false;
static bool s_last_vbus = false;

static void on_pc_connected(void * /*unused*/)
{
    // Safe: runs in LVGL context via lv_async_call.
    ui_terminal_open();
}

static void pc_task(void *)
{
    ESP_LOGI(TAG, "PC connect monitor started");

    while (true) {
        const bool vbus = power_axp2101_is_vbus_present();
        if (vbus && !s_last_vbus) {
            ESP_LOGI(TAG, "USB VBUS detected (PC connected)");
            // Best-effort time sync: if Wi-Fi is connected, SNTP will update time.
            time_service_start_sntp();
            // Auto-open terminal to show live log stream on-device.
            lv_async_call(on_pc_connected, nullptr);
        }
        s_last_vbus = vbus;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t pc_connect_service_init(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_started = true;
    s_last_vbus = power_axp2101_is_vbus_present();

#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(pc_task, "pc_conn", 3072, NULL, 2, NULL);
#else
    // Run the monitor on the non-LVGL core.
    xTaskCreatePinnedToCore(pc_task, "pc_conn", 3072, NULL, 2, NULL, 1);
#endif
    return ESP_OK;
}
