#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// BLE UART (NUS-like) service.
// - TX: notify characteristic
// - RX: write/write-no-rsp characteristic

esp_err_t ble_uart_service_start(void);
void ble_uart_service_stop(void);

bool ble_uart_service_is_running(void);

// Queue bytes to send to the BLE client (if connected + subscribed).
esp_err_t ble_uart_service_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
