#include "services/sdcard_service.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "esp_io_expander_tca9554.h"

#include "i2c_bus.h"
#include "app_pins.h"

static const char *TAG = "sd";
static bool s_mounted = false;
static const char *kMountPoint = "/sdcard";
static esp_err_t s_last_err = ESP_OK;
static char s_last_status[96] = {0};

static void sd_power_cycle(esp_io_expander_handle_t io_expander)
{
    // Only toggle the candidate SD rail pin to avoid disturbing display rails.
    // If this pin isn't actually SD power on a given board spin, the mount retries still proceed.
    if (!io_expander) {
        return;
    }
    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_7, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_7, 1);
    vTaskDelay(pdMS_TO_TICKS(250));
}

esp_err_t sdcard_service_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    s_last_err = ESP_FAIL;
    snprintf(s_last_status, sizeof(s_last_status), "init");

    // Best-effort IO-expander init. Some boards/spins may not require it for SD power.
    esp_io_expander_handle_t io_expander = NULL;
    esp_err_t io_err = esp_io_expander_new_i2c_tca9554(app_i2c_bus(), ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
    if (io_err != ESP_OK) {
        ESP_LOGW(TAG, "tca9554 init failed (continuing without SD power-cycle): %s", esp_err_to_name(io_err));
        io_expander = NULL;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    // Ensure pullups are enabled at the GPIO level too.
    gpio_set_pull_mode(APP_PIN_NUM_SDMMC_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(APP_PIN_NUM_SDMMC_CMD, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(APP_PIN_NUM_SDMMC_D0, GPIO_PULLUP_ONLY);

    struct attempt_cfg_t {
        int slot;
        uint32_t max_freq_khz;
        bool internal_pullups;
    };

    // Start conservative; try slot 1 then slot 0; try with and without internal pullups.
    const attempt_cfg_t attempts[] = {
        {SDMMC_HOST_SLOT_1, 4000, true},
        {SDMMC_HOST_SLOT_1, 4000, false},
        {SDMMC_HOST_SLOT_0, 4000, true},
        {SDMMC_HOST_SLOT_0, 4000, false},
        {SDMMC_HOST_SLOT_1, 10000, true},
        {SDMMC_HOST_SLOT_0, 10000, true},
    };

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < (int)(sizeof(attempts) / sizeof(attempts[0])); i++) {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

        // Avoid long stalls per command when the bus is miswired/unpowered.
        host.command_timeout_ms = 1000;

        host.slot = attempts[i].slot;
        host.max_freq_khz = attempts[i].max_freq_khz;
        // Force 1-bit mode.
        host.flags |= SDMMC_HOST_FLAG_1BIT;

        // Force GPIO-matrix pin mapping for this board.
        slot_config.clk = APP_PIN_NUM_SDMMC_CLK;
        slot_config.cmd = APP_PIN_NUM_SDMMC_CMD;
        slot_config.d0 = APP_PIN_NUM_SDMMC_D0;
        slot_config.width = 1;
        slot_config.cd = SDMMC_SLOT_NO_CD;
        slot_config.wp = SDMMC_SLOT_NO_WP;
        slot_config.flags = 0;
        if (attempts[i].internal_pullups) {
            slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        }

        snprintf(s_last_status,
             sizeof(s_last_status),
             "try %d/%d slot=%d %ukHz %s",
             i + 1,
             (int)(sizeof(attempts) / sizeof(attempts[0])),
             host.slot,
             (unsigned)host.max_freq_khz,
             attempts[i].internal_pullups ? "intPU" : "extPU");

        ESP_LOGI(TAG, "Mount attempt %d/%d: slot=%d freq=%ukHz pullups=%s pins(CMD=%d CLK=%d D0=%d)",
             i + 1,
             (int)(sizeof(attempts) / sizeof(attempts[0])),
             host.slot,
             (unsigned)host.max_freq_khz,
             attempts[i].internal_pullups ? "internal" : "external-only",
             (int)slot_config.cmd,
             (int)slot_config.clk,
             (int)slot_config.d0);

        // After the first failure, try a best-effort power cycle before subsequent retries.
        if (i > 0) {
            sd_power_cycle(io_expander);
        }

        sdmmc_card_t *card = NULL;
        ret = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            s_mounted = true;
            s_last_err = ESP_OK;
            snprintf(s_last_status, sizeof(s_last_status), "mounted");
            ESP_LOGI(TAG, "Mounted at %s", kMountPoint);
            if (card) {
                sdmmc_card_print_info(stdout, card);
            }
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(ret));
        s_mounted = false;
        s_last_err = ret;
        snprintf(s_last_status, sizeof(s_last_status), "fail: %s", esp_err_to_name(ret));
    }

    return ret;
}

const char *sdcard_service_last_status(void)
{
    return s_last_status;
}

bool sdcard_service_is_mounted(void)
{
    return s_mounted;
}

const char *sdcard_service_mount_point(void)
{
    return kMountPoint;
}

esp_err_t sdcard_service_last_error(void)
{
    return s_last_err;
}
