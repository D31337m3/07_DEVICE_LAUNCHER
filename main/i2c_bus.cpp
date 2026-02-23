#include "i2c_bus.h"

#include "app_pins.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";
static i2c_master_bus_handle_t s_bus = nullptr;

i2c_master_bus_handle_t app_i2c_bus(void)
{
    return s_bus;
}

esp_err_t app_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = APP_PIN_NUM_I2C_SDA,
        .scl_io_num = APP_PIN_NUM_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };

    ESP_LOGI(TAG, "Initializing I2C master bus on SCL=%d, SDA=%d",
             (int)APP_PIN_NUM_I2C_SCL, (int)APP_PIN_NUM_I2C_SDA);
    return i2c_new_master_bus(&bus_cfg, &s_bus);
}
