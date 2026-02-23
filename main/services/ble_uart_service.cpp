#include "services/ble_uart_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/util/util.h"

#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Provided by NimBLE store config implementation.
extern "C" void ble_store_config_init(void);

static const char *TAG = "ble_uart";

// NUS UUIDs
static const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t kRxUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t kTxUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static bool s_running = false;

static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle = 0;
static bool s_notify_enabled = false;

static RingbufHandle_t s_tx_rb = nullptr;
static TaskHandle_t s_tx_task = nullptr;

static vprintf_like_t s_prev_vprintf = nullptr;

static void start_advertising(void);

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *)
{
    (void)conn_handle;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // RX characteristic
        const uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
        (void)uuid16;

        const uint16_t len = (uint16_t)OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0) {
            return 0;
        }
        uint8_t buf[256];
        const uint16_t to_copy = (len > sizeof(buf)) ? (uint16_t)sizeof(buf) : len;
        os_mbuf_copydata(ctxt->om, 0, to_copy, buf);

        // Best-effort: echo to UART/log as a "serial input".
        // (No command parser here; this just makes input visible.)
        fwrite(buf, 1, to_copy, stdout);
        fflush(stdout);
        return 0;
    }

    // No readable characteristics.
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def kGattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kSvcUuid.u,
        .includes = nullptr,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &kRxUuid.u,
                .access_cb = gatt_access_cb,
                .arg = nullptr,
                .descriptors = nullptr,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .min_key_size = 0,
                .val_handle = nullptr,
                .cpfd = nullptr,
            },
            {
                .uuid = &kTxUuid.u,
                .access_cb = gatt_access_cb,
                .arg = nullptr,
                .descriptors = nullptr,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle = &s_tx_val_handle,
                .cpfd = nullptr,
            },
            {},
        },
    },
    {},
};

static int gap_event_cb(struct ble_gap_event *event, void *)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_notify_enabled = false;
                ESP_LOGI(TAG, "Connected (handle=%u)", (unsigned)s_conn_handle);
            } else {
                ESP_LOGI(TAG, "Connect failed; restarting advertising");
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected");
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_notify_enabled = false;
            start_advertising();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_tx_val_handle) {
                s_notify_enabled = event->subscribe.cur_notify != 0;
                ESP_LOGI(TAG, "Notify %s", s_notify_enabled ? "enabled" : "disabled");
            }
            return 0;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU updated: %u", (unsigned)event->mtu.value);
            return 0;
        default:
            return 0;
    }
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Advertise the service UUID.
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.uuids128 = (ble_uuid128_t *)&kSvcUuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    // Name
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    (void)ble_gap_adv_set_fields(&fields);
    (void)ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER, &adv_params, gap_event_cb, nullptr);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    start_advertising();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void tx_task(void *)
{
    while (true) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(s_tx_rb, &item_size, pdMS_TO_TICKS(200));
        if (!item) {
            continue;
        }

        // If not connected/subscribed, drop.
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled || s_tx_val_handle == 0) {
            vRingbufferReturnItem(s_tx_rb, item);
            continue;
        }

        // Send in chunks.
        size_t offset = 0;
        while (offset < item_size) {
            const size_t chunk_len = (item_size - offset > 180) ? 180 : (item_size - offset);
            struct os_mbuf *om = ble_hs_mbuf_from_flat(item + offset, chunk_len);
            if (!om) {
                break;
            }
            int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
            if (rc != 0) {
                break;
            }
            offset += chunk_len;
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        vRingbufferReturnItem(s_tx_rb, item);
    }
}

static int ble_uart_log_vprintf(const char *fmt, va_list args)
{
    // Forward to previous logger first.
    int ret = 0;
    if (s_prev_vprintf) {
        va_list args_fwd;
        va_copy(args_fwd, args);
        ret = s_prev_vprintf(fmt, args_fwd);
        va_end(args_fwd);
    } else {
        va_list args_fwd;
        va_copy(args_fwd, args);
        ret = vprintf(fmt, args_fwd);
        va_end(args_fwd);
    }

    // Format into a buffer for BLE TX.
    if (s_tx_rb) {
        char buf[256];
        va_list args_copy;
        va_copy(args_copy, args);
        int n = vsnprintf(buf, sizeof(buf), fmt, args_copy);
        va_end(args_copy);
        if (n > 0) {
            const size_t send_len = (n > (int)sizeof(buf)) ? sizeof(buf) : (size_t)n;
            (void)xRingbufferSend(s_tx_rb, buf, send_len, 0);
        }
    }

    return ret;
}

esp_err_t ble_uart_service_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    if (!s_tx_rb) {
        s_tx_rb = xRingbufferCreate(8192, RINGBUF_TYPE_NOSPLIT);
        if (!s_tx_rb) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_tx_task) {
        xTaskCreatePinnedToCore(tx_task, "ble_uart_tx", 4096, nullptr, 3, &s_tx_task, 1);
    }

    // Install BLE logger wrapper (so BLE behaves like a serial monitor).
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(ble_uart_log_vprintf);
    }

    // Bring up NimBLE stack.
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    int rc = ble_gatts_count_cfg(kGattSvcs);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(kGattSvcs);
    if (rc != 0) {
        return ESP_FAIL;
    }

    // Set device name.
    ble_svc_gap_device_name_set("DeviceLauncher");

    nimble_port_freertos_init(host_task);

    s_running = true;
    ESP_LOGI(TAG, "BLE UART started");
    return ESP_OK;
}

void ble_uart_service_stop(void)
{
    if (!s_running) {
        return;
    }

    // Stop advertising / disconnect.
    (void)ble_gap_adv_stop();
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;

    nimble_port_stop();
    nimble_port_deinit();

    // Restore previous logger.
    if (s_prev_vprintf) {
        esp_log_set_vprintf(s_prev_vprintf);
        s_prev_vprintf = nullptr;
    }

    // Leave ringbuffer/task in place (cheap) so send() works immediately on restart.
    s_running = false;
}

bool ble_uart_service_is_running(void)
{
    return s_running;
}

esp_err_t ble_uart_service_send(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_tx_rb) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xRingbufferSend(s_tx_rb, data, len, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
