#include "services/boot_service.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_timer.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "lvgl.h"

#include "display_lvgl.h"
#include "i2c_bus.h"

#include "services/audio_es8311.h"
#include "lvgl_fs_sdcard.h"
#include "services/ble_service.h"
#include "services/imu_qmi8658.h"
#include "services/power_axp2101.h"
#include "services/power_manager.h"
#include "services/time_service.h"
#include "services/wifi_service.h"
#include "services/pc_connect_service.h"
#include "services/app_manager.h"
#include "services/storage_service.h"

static const char *TAG = "boot";

// Embedded logo image
extern const uint8_t logo_png_start[] asm("_binary_logo_png_start");
extern const uint8_t logo_png_end[] asm("_binary_logo_png_end");

static void show_boot_splash(void)
{
    if (!display_lvgl_lock(100)) {
        return;
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_img_dsc_t logo_img;
    logo_img.header.always_zero = 0;
    logo_img.header.w = 0;
    logo_img.header.h = 0;
    logo_img.header.cf = LV_IMG_CF_RAW_ALPHA;
    logo_img.data_size = logo_png_end - logo_png_start;
    logo_img.data = logo_png_start;

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &logo_img);
    lv_obj_center(img);

    lv_obj_t *lbl_sub = lv_label_create(scr);
    lv_label_set_text(lbl_sub, "Tiny OS   - Beta 0.1");
    lv_obj_set_style_text_color(lbl_sub, lv_color_white(), 0);
    lv_obj_align_to(lbl_sub, img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t *bar = lv_bar_create(scr);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(bar, 220, 10);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_border_color(bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_scr_load(scr);
    display_lvgl_unlock();

    // Play spray paint rattle sound while the logo is on-screen.
    audio_es8311_play_spray_rattle();

    // Keep the splash for 8 seconds while updating progress.
    const int kDurationMs = 8000;
    const int kUpdateMs = 50;
    const int64_t start_us = esp_timer_get_time();

    while (true) {
        const int64_t elapsed_ms64 = (esp_timer_get_time() - start_us) / 1000;
        if (elapsed_ms64 >= kDurationMs) {
            break;
        }

        int progress = (int)((elapsed_ms64 * 100) / kDurationMs);
        if (progress < 0) progress = 0;
        if (progress > 100) progress = 100;

        if (display_lvgl_lock(20)) {
            lv_bar_set_value(bar, progress, LV_ANIM_OFF);
            display_lvgl_unlock();
        }

        int remaining_ms = kDurationMs - (int)elapsed_ms64;
        if (remaining_ms <= 0) {
            break;
        }
        const int sleep_ms = (remaining_ms < kUpdateMs) ? remaining_ms : kUpdateMs;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }

    if (display_lvgl_lock(50)) {
        lv_bar_set_value(bar, 100, LV_ANIM_OFF);
        display_lvgl_unlock();
    }
}

static void show_flash_success_screen(void)
{
    if (!display_lvgl_lock(100)) {
        return;
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x00AA00), 0);

    lv_obj_t *lbl_check = lv_label_create(scr);
    lv_label_set_text(lbl_check, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(lbl_check, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_check, lv_color_white(), 0);
    lv_obj_align(lbl_check, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl_text = lv_label_create(scr);
    lv_label_set_text(lbl_text, "Firmware Updated\nSuccessfully!");
    lv_obj_set_style_text_align(lbl_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl_text, lv_color_white(), 0);
    lv_obj_align(lbl_text, LV_ALIGN_CENTER, 0, 30);

    lv_scr_load(scr);
    display_lvgl_unlock();

    vTaskDelay(pdMS_TO_TICKS(3000));
}

static bool check_first_boot_after_flash(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();

    uint32_t current_build = 0;
    for (int i = 0; app_desc->date[i] != '\0' && i < 16; i++) {
        current_build = current_build * 31 + (uint8_t)app_desc->date[i];
    }
    for (int i = 0; app_desc->time[i] != '\0' && i < 16; i++) {
        current_build = current_build * 31 + (uint8_t)app_desc->time[i];
    }

    uint32_t stored_build = 0;
    err = nvs_get_u32(nvs_handle, "fw_build", &stored_build);

    const bool is_first_boot = (err != ESP_OK || stored_build != current_build);

    nvs_set_u32(nvs_handle, "fw_build", current_build);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return is_first_boot;
}

esp_err_t boot_service_init(boot_service_result_t *out_result)
{
    if (out_result) {
        out_result->first_boot_after_flash = false;
    }

    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "nvs init failed");

    ESP_LOGI(TAG, "Init I2C");
    ESP_RETURN_ON_ERROR(app_i2c_init(), TAG, "i2c init failed");

    ESP_LOGI(TAG, "Init display + LVGL");
    ESP_RETURN_ON_ERROR(display_lvgl_init(), TAG, "display/lvgl init failed");

    // Enable LVGL file access for SD card images (S:/ -> /sdcard/)
    lvgl_fs_sdcard_init();

    ESP_LOGI(TAG, "Init audio");
    ESP_RETURN_ON_ERROR(audio_es8311_init(), TAG, "audio init failed");

    ESP_LOGI(TAG, "Showing boot splash");
    show_boot_splash();

    const bool first_boot = check_first_boot_after_flash();
    if (out_result) {
        out_result->first_boot_after_flash = first_boot;
    }

    ESP_LOGI(TAG, "Init remaining services");
    power_axp2101_init(app_i2c_bus());
    time_service_init(app_i2c_bus());
    imu_qmi8658_init(app_i2c_bus());
    ble_service_init();
    wifi_service_init();

    // Mount internal flash storage early so app_manager can scan /storage/apps.
    (void)storage_service_mount();
    app_manager_init();
    power_manager_init();
    pc_connect_service_init();

    if (first_boot) {
        ESP_LOGI(TAG, "First boot after flash - showing success screen");
        show_flash_success_screen();
    }

    return ESP_OK;
}
