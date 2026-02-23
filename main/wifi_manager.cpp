#include "wifi_manager.h"
#include "radio_player.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"

static const char *TAG = "wifi_manager";

static esp_err_t wifi_manager_http_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Scan WiFi networks
        wifi_scan_config_t scan_config = {0};
        esp_wifi_scan_start(&scan_config, true);
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num);
        esp_wifi_scan_get_ap_records(&ap_num, ap_records);

        // Build HTML with scanned SSIDs and radio control
        httpd_resp_sendstr(req, "<html><body><h2>WiFi Manager</h2>");
        httpd_resp_sendstr(req, "<form method='POST'>SSID: <select name='ssid'>");
        for (int i = 0; i < ap_num; ++i) {
            httpd_resp_sendstr(req, "<option value='");
            httpd_resp_sendstr(req, (const char *)ap_records[i].ssid);
            httpd_resp_sendstr(req, "'>");
            httpd_resp_sendstr(req, (const char *)ap_records[i].ssid);
            httpd_resp_sendstr(req, "</option>");
        }
        httpd_resp_sendstr(req, "</select><br>Password: <input name='password' type='password'><br>");
        httpd_resp_sendstr(req, "<input type='submit' value='Connect'></form>");
        free(ap_records);

        // Radio control UI
        httpd_resp_sendstr(req, "<hr><h2>Internet Radio</h2>");
        httpd_resp_sendstr(req, "<form method='POST' action='/radio'>");
        httpd_resp_sendstr(req, "Station: <select name='station'>");
        httpd_resp_sendstr(req, "<option value='http://icecast.omroep.nl/radio1-bb-mp3'>Radio 1</option>");
        httpd_resp_sendstr(req, "<option value='http://icecast.omroep.nl/radio2-bb-mp3'>Radio 2</option>");
        httpd_resp_sendstr(req, "<option value='http://icecast.omroep.nl/3fm-bb-mp3'>3FM</option>");
        httpd_resp_sendstr(req, "<option value='http://icecast.omroep.nl/radio4-bb-mp3'>Radio 4</option>");
        httpd_resp_sendstr(req, "<option value='http://icecast.omroep.nl/radio5-bb-mp3'>Radio 5</option>");
        httpd_resp_sendstr(req, "</select><br><input type='submit' value='Play'></form></body></html>");
        return ESP_OK;
    } else if (req->method == HTTP_POST) {
        // Parse POST data for SSID and password
        char buf[128];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        buf[len] = '\0';
        char ssid[32] = {0}, password[64] = {0};
        sscanf(buf, "ssid=%31[^&]&password=%63s", ssid, password);

        // Connect to WiFi
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_connect();

        httpd_resp_send(req, "<html><body><h2>Connecting...</h2></body></html>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t radio_control_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[len] = '\0';
    char station_url[128] = {0};
    sscanf(buf, "station=%127s", station_url);
    radio_player_play(station_url);
    httpd_resp_send(req, "<html><body><h2>Playing...</h2></body></html>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    // WiFi (netif, event loop, STA mode, start) is fully handled by wifi_service_init()
    // inside boot_service_init(). We only need to start the HTTP server here.

    // Start HTTP server
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &server_config) == ESP_OK) {
        httpd_uri_t wifi_uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = wifi_manager_http_handler,
            .user_ctx = NULL
        };
        httpd_uri_t wifi_uri_post = {
            .uri = "/",
            .method = HTTP_POST,
            .handler = wifi_manager_http_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_uri_get);
        httpd_register_uri_handler(server, &wifi_uri_post);

        // Radio control endpoint
        httpd_uri_t radio_uri_post = {
            .uri = "/radio",
            .method = HTTP_POST,
            .handler = radio_control_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &radio_uri_post);
    }
    return ESP_OK;
}
