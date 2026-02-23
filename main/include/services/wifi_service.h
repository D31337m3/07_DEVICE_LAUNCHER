#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_service_init(void);
esp_err_t wifi_service_connect(const char *ssid, const char *pass);
bool wifi_service_is_connected(void);
void wifi_service_get_ip(char *out, int out_len);

// Load saved credentials (if any). Returns true if an SSID was present.
bool wifi_service_get_saved_credentials(char *ssid_out, size_t ssid_out_len, char *pass_out, size_t pass_out_len);

// WiFi scanning
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_ap_record_simple_t;

esp_err_t wifi_service_scan(wifi_ap_record_simple_t *list, uint16_t *count, uint16_t max_count);

#ifdef __cplusplus
}
#endif
