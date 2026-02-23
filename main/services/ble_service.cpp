#include "services/ble_service.h"

#include "esp_log.h"

#include "nvs.h"

#include "services/ble_uart_service.h"

static const char *TAG = "ble";
static bool s_enabled = false;

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("ble", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, "en", &v) == ESP_OK) {
        s_enabled = (v != 0);
    }
    nvs_close(h);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open("ble", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "en", s_enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t ble_service_init(void)
{
    nvs_load();
#if CONFIG_BT_NIMBLE_ENABLED
    // Minimal stub for now: full GATT/advertising wiring is board/app specific.
    ESP_LOGI(TAG, "NimBLE enabled in sdkconfig; BLE can be wired up in ble_service_set_enabled()");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "BT/NimBLE not enabled in sdkconfig");
    return ESP_OK;
#endif
}

esp_err_t ble_service_set_enabled(bool enabled)
{
    s_enabled = enabled;
    nvs_save();
    ESP_LOGI(TAG, "BLE enabled=%d", (int)s_enabled);

#if CONFIG_BT_NIMBLE_ENABLED
    if (s_enabled) {
        (void)ble_uart_service_start();
    } else {
        ble_uart_service_stop();
    }
#endif
    return ESP_OK;
}

bool ble_service_is_enabled(void)
{
    return s_enabled;
}
