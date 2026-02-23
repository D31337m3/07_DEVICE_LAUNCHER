#include "services/power_axp2101.h"

#include <string.h>

#include "esp_log.h"

#include "i2c_bus.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "power";

static i2c_master_dev_handle_t s_axp_dev = nullptr;
static XPowersPMU s_pmu;
static bool s_ready = false;

static int pmu_i2c_read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (len == 0 || data == nullptr || s_axp_dev == nullptr) {
        return -1;
    }
    esp_err_t ret = i2c_master_transmit_receive(s_axp_dev, &regAddr, 1, data, len, 1000);
    return (ret == ESP_OK) ? 0 : -1;
}

static int pmu_i2c_write(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (data == nullptr || s_axp_dev == nullptr) {
        return -1;
    }
    uint8_t write_buf[len + 1];
    write_buf[0] = regAddr;
    if (len > 0) {
        memcpy(&write_buf[1], data, len);
    }
    esp_err_t ret = i2c_master_transmit(s_axp_dev, write_buf, len + 1, 1000);
    return (ret == ESP_OK) ? 0 : -1;
}

bool power_axp2101_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        ESP_LOGW(TAG, "Invalid I2C bus handle");
        return false;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_SLAVE_ADDRESS,
        .scl_speed_hz = 200000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_axp_dev) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add AXP2101 device");
        s_ready = false;
        return false;
    }
    if (s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_i2c_read, pmu_i2c_write)) {
        s_ready = true;
        ESP_LOGI(TAG, "AXP2101 init OK, batt=%d%%", s_pmu.getBatteryPercent());
        s_pmu.enableBattVoltageMeasure();
        s_pmu.enableVbusVoltageMeasure();
        s_pmu.enableSystemVoltageMeasure();
        return true;
    }
    ESP_LOGW(TAG, "AXP2101 not detected");
    s_ready = false;
    return false;
}

int power_axp2101_get_battery_percent(void)
{
    if (!s_ready) {
        return -1;
    }
    return s_pmu.getBatteryPercent();
}

int power_axp2101_get_batt_mv(void)
{
    if (!s_ready) {
        return -1;
    }
    return s_pmu.getBattVoltage();
}

bool power_axp2101_is_charging(void)
{
    if (!s_ready) {
        return false;
    }
    return s_pmu.isCharging();
}

bool power_axp2101_is_vbus_present(void)
{
    if (!s_ready) {
        return false;
    }
    // XPowersAXP2101: VBUS present + good.
    return s_pmu.isVbusIn();
}
