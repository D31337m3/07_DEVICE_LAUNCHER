#include "services/wifi_service.h"

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

static esp_netif_t *s_netif = nullptr;
static bool s_connected = false;
static esp_ip4_addr_t s_ip = {};

static bool s_scan_in_progress = false;

static int ap_record_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *rb = (const wifi_ap_record_t *)b;
    // Sort by RSSI (strongest first)
    return (rb->rssi - ra->rssi);
}

static void on_wifi_event(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            memset(&s_ip, 0, sizeof(s_ip));
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_ip = event->ip_info.ip;
        s_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&s_ip));
    }
}

static void nvs_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "ssid", ssid ? ssid : "");
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}

static bool nvs_load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t s_len = ssid_len;
    size_t p_len = pass_len;
    esp_err_t s_err = nvs_get_str(h, "ssid", ssid, &s_len);
    esp_err_t p_err = nvs_get_str(h, "pass", pass, &p_len);
    nvs_close(h);
    return (s_err == ESP_OK && p_err == ESP_OK && ssid[0] != '\0');
}

bool wifi_service_get_saved_credentials(char *ssid_out, size_t ssid_out_len, char *pass_out, size_t pass_out_len)
{
    if (!ssid_out || ssid_out_len == 0 || !pass_out || pass_out_len == 0) {
        return false;
    }
    ssid_out[0] = '\0';
    pass_out[0] = '\0';
    return nvs_load_creds(ssid_out, ssid_out_len, pass_out, pass_out_len);
}

esp_err_t wifi_service_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");

    s_netif = esp_netif_create_default_wifi_sta();
    assert(s_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL), TAG, "wifi handler reg failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL), TAG, "ip handler reg failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");

    // Load saved credentials if any
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        wifi_config_t wifi_config = {};
        memcpy(wifi_config.sta.ssid, ssid, strnlen(ssid, sizeof(wifi_config.sta.ssid) - 1));
        memcpy(wifi_config.sta.password, pass, strnlen(pass, sizeof(wifi_config.sta.password) - 1));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set config failed");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    return ESP_OK;
}

esp_err_t wifi_service_connect(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t wifi_config = {};
    memcpy(wifi_config.sta.ssid, ssid, strnlen(ssid, sizeof(wifi_config.sta.ssid) - 1));
    if (pass) {
        memcpy(wifi_config.sta.password, pass, strnlen(pass, sizeof(wifi_config.sta.password) - 1));
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set config failed");
    nvs_save_creds(ssid, pass ? pass : "");
    return esp_wifi_connect();
}

bool wifi_service_is_connected(void)
{
    return s_connected;
}

void wifi_service_get_ip(char *out, int out_len)
{
    if (!out || out_len <= 0) {
        return;
    }
    if (!s_connected) {
        strncpy(out, "-", out_len);
        return;
    }
    snprintf(out, out_len, IPSTR, IP2STR(&s_ip));
}

esp_err_t wifi_service_scan(wifi_ap_record_simple_t *list, uint16_t *count, uint16_t max_count)
{
    if (!list || !count || max_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Start scan (stop any previous scan first)
    (void)esp_wifi_scan_stop();
    s_scan_in_progress = true;

    wifi_scan_config_t scan_config = {};
    scan_config.ssid = NULL;
    scan_config.bssid = NULL;
    scan_config.channel = 0;
    scan_config.show_hidden = true;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 120;
    scan_config.scan_time.active.max = 600;

    esp_err_t scan_err = esp_wifi_scan_start(&scan_config, true);
    s_scan_in_progress = false;
    ESP_RETURN_ON_ERROR(scan_err, TAG, "scan start failed");

    // Get results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Scan complete: %u APs", (unsigned)ap_count);
    
    if (ap_count == 0) {
        *count = 0;
        return ESP_OK;
    }

    // Limit to max_count
    uint16_t fetch_count = (ap_count > max_count) ? max_count : ap_count;
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * fetch_count);
    if (!ap_records) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t rec_err = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
    if (rec_err != ESP_OK) {
        free(ap_records);
        return rec_err;
    }

    qsort(ap_records, fetch_count, sizeof(wifi_ap_record_t), ap_record_rssi_desc);

    // Copy to simplified structure
    for (uint16_t i = 0; i < fetch_count; i++) {
        strncpy(list[i].ssid, (char *)ap_records[i].ssid, sizeof(list[i].ssid) - 1);
        list[i].ssid[sizeof(list[i].ssid) - 1] = '\0';
        list[i].rssi = ap_records[i].rssi;
        list[i].authmode = ap_records[i].authmode;
    }

    free(ap_records);
    *count = fetch_count;
    return ESP_OK;
}
