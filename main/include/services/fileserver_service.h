#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts WiFi SoftAP + an HTTP server on port 80.
// The server exposes a minimal web UI to upload/download and edit files on:
// - flash: /storage
// - sd:    /sdcard (only if mounted)

esp_err_t fileserver_service_start(void);
void fileserver_service_stop(void);

bool fileserver_service_is_running(void);

// Returns a stable pointer to the current AP SSID string.
const char *fileserver_service_ap_ssid(void);

// Convenience: AP default IP (typically 192.168.4.1).
const char *fileserver_service_ap_ip(void);

#ifdef __cplusplus
}
#endif
