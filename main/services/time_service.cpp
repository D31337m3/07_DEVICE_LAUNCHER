#include "services/time_service.h"

#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "i2c_bus.h"

static const char *TAG = "time";

static i2c_master_dev_handle_t s_rtc_dev = nullptr;
static bool s_has_rtc = false;

static constexpr uint8_t kPcf85063Addr = 0x51;

static uint8_t bcd_to_dec(uint8_t val)
{
    return (uint8_t)((val >> 4) * 10 + (val & 0x0F));
}

static esp_err_t rtc_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!data || len == 0 || s_rtc_dev == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_rtc_dev, &reg_addr, 1, data, len, 1000);
}

static bool rtc_try_set_system_time(void)
{
    uint8_t data[7] = {0};
    // time registers start at 0x04
    if (rtc_read(0x04, data, sizeof(data)) != ESP_OK) {
        return false;
    }
    const int sec = bcd_to_dec(data[0] & 0x7F);
    const int min = bcd_to_dec(data[1] & 0x7F);
    const int hour = bcd_to_dec(data[2] & 0x3F);
    const int mday = bcd_to_dec(data[3] & 0x3F);
    const int mon = bcd_to_dec(data[5] & 0x1F);
    const int year = 2000 + bcd_to_dec(data[6]);

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    time_t epoch = mktime(&t);
    if (epoch <= 0) {
        return false;
    }
    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "System time set from PCF85063");
    return true;
}

bool time_service_init(i2c_master_bus_handle_t bus)
{
    s_has_rtc = false;
    if (!bus) return false;
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kPcf85063Addr,
        .scl_speed_hz = 200000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_rtc_dev) != ESP_OK) {
        return false;
    }
    s_has_rtc = rtc_try_set_system_time();
    if (s_has_rtc) {
        ESP_LOGI(TAG, "PCF85063 RTC found, time synced");
    } else {
        ESP_LOGW(TAG, "RTC not detected or invalid time; will rely on SNTP when Wi-Fi is available");
    }
    return true;
}

void time_service_start_sntp(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov");
    sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

void time_service_format(char *out_time, size_t out_time_len, char *out_date, size_t out_date_len)
{
    time_t now = 0;
    time(&now);
    struct tm t = {};
    localtime_r(&now, &t);
    if (out_time && out_time_len) {
        strftime(out_time, out_time_len, "%H:%M", &t);
    }
    if (out_date && out_date_len) {
        strftime(out_date, out_date_len, "%Y-%m-%d", &t);
    }
}

bool time_service_get_localtime(struct tm *out)
{
    if (!out) {
        return false;
    }
    time_t now = 0;
    time(&now);
    if (now < 100000) {
        // Not set (around 1970).
        memset(out, 0, sizeof(*out));
        return false;
    }
    localtime_r(&now, out);
    return true;
}
