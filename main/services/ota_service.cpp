#include "services/ota_service.h"

#include "esp_https_ota.h"
#include "esp_log.h"

static const char *TAG = "ota";

static void ota_task(void *arg)
{
    const char *url = (const char *)arg;
    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };
    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA success, rebooting");
        esp_restart();
    } else {
        ESP_LOGW(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    free(arg);
    vTaskDelete(NULL);
}

esp_err_t ota_service_start_from_url(const char *url)
{
    if (!url || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char *heap_url = (char *)malloc(strlen(url) + 1);
    if (!heap_url) {
        return ESP_ERR_NO_MEM;
    }
    strcpy(heap_url, url);
    xTaskCreate(ota_task, "ota", 8192, heap_url, 5, NULL);
    return ESP_OK;
}
