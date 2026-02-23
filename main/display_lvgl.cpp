#include "display_lvgl.h"

#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "nvs.h"

#include "lvgl.h"

#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"

#include "esp_io_expander_tca9554.h"

#include "app_pins.h"
#include "i2c_bus.h"

static const char *TAG = "display";

static SemaphoreHandle_t s_lvgl_mux = nullptr;
static esp_lcd_touch_handle_t s_touch = nullptr;
static esp_lcd_panel_handle_t s_panel_handle = nullptr;
static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static uint8_t s_brightness = 100;
static bool s_sh8601_qspi = true;

// SH8601 QSPI interface requires an opcode prefix on commands.
// See 05_LVGL_WITH_RAM/components/esp_lcd_sh8601/esp_lcd_sh8601.c tx_param().
#define SH8601_LCD_OPCODE_WRITE_CMD (0x02U)

static inline int sh8601_encode_cmd(int lcd_cmd)
{
    if (!s_sh8601_qspi) {
        return lcd_cmd;
    }
    lcd_cmd &= 0xFF;
    lcd_cmd <<= 8;
    lcd_cmd |= (int)(SH8601_LCD_OPCODE_WRITE_CMD << 24);
    return lcd_cmd;
}

static void nvs_load_brightness(void)
{
    nvs_handle_t h;
    if (nvs_open("disp", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, "bri", &v) == ESP_OK && v <= 100) {
        s_brightness = v;
    }
    nvs_close(h);
}

static void nvs_save_brightness(void)
{
    nvs_handle_t h;
    if (nvs_open("disp", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "bri", s_brightness);
    nvs_commit(h);
    nvs_close(h);
}

static void apply_brightness_hw(uint8_t brightness_percent)
{
    if (!s_panel_io) {
        return;
    }
    if (brightness_percent > 100) brightness_percent = 100;

    uint8_t val = (uint8_t)((brightness_percent * 255U) / 100U);
    if (brightness_percent > 0 && val == 0) val = 1;

    const uint8_t ctrl = 0x20;
    (void)esp_lcd_panel_io_tx_param(s_panel_io, sh8601_encode_cmd(0x53), &ctrl, 1);
    esp_err_t err = esp_lcd_panel_io_tx_param(s_panel_io, sh8601_encode_cmd(0x51), &val, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "brightness tx failed: %s", esp_err_to_name(err));
    }
}

#define LCD_HOST SPI2_HOST

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#else
#error "Unsupported LVGL color depth"
#endif

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2

// Keep LVGL off CPU0 so Wi-Fi + system tasks and IDLE0 can run and feed the task watchdog.
// This is especially important during heavy JPG/GIF rendering.
#define LVGL_TASK_CORE 1

// If the LCD IO queue wedges (e.g., missed DMA done callback or a stuck SPI transaction),
// LVGL can remain in a perpetual "flushing" state and starve IDLE0, triggering the task WDT.
// Use a conservative timeout to fail-open and keep the UI responsive.
#define LVGL_FLUSH_TIMEOUT_MS 1000

static volatile bool s_flush_inflight = false;
static volatile uint32_t s_flush_start_ms = 0;
static lv_disp_drv_t *s_disp_drv_ptr = nullptr;

static const sh8601_lcd_init_cmd_t kLcdInitCmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    s_flush_inflight = false;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

#if LCD_BIT_PER_PIXEL == 24
    uint8_t *to = (uint8_t *)color_map;
    uint16_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);

    uint8_t temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    for (int i = 1; i < pixel_num; i++) {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    s_flush_inflight = true;
    s_flush_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    if (err != ESP_OK) {
        // If the transfer wasn't queued, the IO "flush ready" callback will never fire.
        // Unblock LVGL to avoid a permanent busy state + task watchdog.
        static uint32_t s_last_log_ms = 0;
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now_ms - s_last_log_ms > 1000) {
            s_last_log_ms = now_ms;
            ESP_LOGW(TAG, "panel_draw_bitmap failed: %s (unblocking LVGL)", esp_err_to_name(err));
        }
        s_flush_inflight = false;
        lv_disp_flush_ready(drv);
    }
}

static void lvgl_update_cb(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

static void lvgl_wait_cb(lv_disp_drv_t *drv)
{
    // LVGL can enter a busy-wait loop (while(draw_buf->flushing)) if it believes a previous flush
    // is still in progress. If the panel IO completion callback is missed (e.g., SPI queue failure),
    // LVGL will spin forever unless we yield here.
    //
    // Only yield when a flush is actually in flight; otherwise this unnecessarily throttles
    // `lv_timer_handler()` and reduces UI responsiveness.
    if (!s_flush_inflight) {
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    if (s_flush_inflight) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now_ms - s_flush_start_ms > LVGL_FLUSH_TIMEOUT_MS) {
            static uint32_t s_last_timeout_log_ms = 0;
            if (now_ms - s_last_timeout_log_ms > 2000) {
                s_last_timeout_log_ms = now_ms;
                ESP_LOGE(TAG, "LVGL flush timeout in wait_cb (> %u ms). Forcing flush_ready().", LVGL_FLUSH_TIMEOUT_MS);
            }
            s_flush_inflight = false;
            lv_disp_flush_ready(drv);
        }
    }
}

static void lvgl_rounder_cb(lv_disp_drv_t *, lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    assert(tp);

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (pressed && tp_cnt > 0) {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void increase_lvgl_tick(void *)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool display_lvgl_lock(int timeout_ms)
{
    assert(s_lvgl_mux && "display_lvgl_init must be called first");
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, timeout_ticks) == pdTRUE;
}

void display_lvgl_unlock(void)
{
    assert(s_lvgl_mux && "display_lvgl_init must be called first");
    xSemaphoreGive(s_lvgl_mux);
}

static void lvgl_task(void *)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    while (true) {
        if (display_lvgl_lock(-1)) {
            if (s_flush_inflight && s_disp_drv_ptr) {
                uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                if (now_ms - s_flush_start_ms > LVGL_FLUSH_TIMEOUT_MS) {
                    static uint32_t s_last_timeout_log_ms = 0;
                    if (now_ms - s_last_timeout_log_ms > 2000) {
                        s_last_timeout_log_ms = now_ms;
                        ESP_LOGE(TAG, "LVGL flush timeout (> %u ms). Forcing flush_ready() to avoid WDT.", LVGL_FLUSH_TIMEOUT_MS);
                    }
                    s_flush_inflight = false;
                    lv_disp_flush_ready(s_disp_drv_ptr);
                }
            }
            task_delay_ms = lv_timer_handler();
            display_lvgl_unlock();
        }
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

esp_err_t display_lvgl_init(void)
{
    // NVS is initialized in boot_service before display init.
    nvs_load_brightness();

    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    // I2C is already initialized in main(), no need to reinitialize

    esp_io_expander_handle_t io_expander = NULL;
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(app_i2c_bus(), ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander), TAG,
                        "tca9554 init failed");
    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 1);

    ESP_LOGI(TAG, "Initialize SPI bus (QSPI)");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        APP_PIN_NUM_LCD_PCLK,
        APP_PIN_NUM_LCD_DATA0,
        APP_PIN_NUM_LCD_DATA1,
        APP_PIN_NUM_LCD_DATA2,
        APP_PIN_NUM_LCD_DATA3,
        APP_LCD_H_RES * APP_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi_bus_initialize failed");

    ESP_LOGI(TAG, "Install panel IO");
    static lv_disp_drv_t disp_drv;
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(APP_PIN_NUM_LCD_CS, notify_lvgl_flush_ready, &disp_drv);
    // Push more bandwidth than the driver default (40MHz) for better FPS on QSPI panels.
    // If you see flicker/tearing/IO timeouts on a specific board spin, reduce this back to 40MHz.
    io_config.pclk_hz = 80 * 1000 * 1000;

    // Media/JPG/GIF rendering can enqueue many small transfers; give more headroom to avoid queue saturation.
    io_config.trans_queue_depth = 32;
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = kLcdInitCmds,
        .init_cmds_size = sizeof(kLcdInitCmds) / sizeof(kLcdInitCmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    s_sh8601_qspi = vendor_config.flags.use_qspi_interface != 0;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle), TAG,
                        "esp_lcd_new_panel_io_spi failed");
    s_panel_io = io_handle;

    const esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .reset_gpio_num = APP_PIN_NUM_LCD_RST,
        .vendor_config = &vendor_config,
        .flags = {
            .reset_active_high = 0,
        },
    };
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &s_panel_handle), TAG, "esp_lcd_new_panel_sh8601 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel_handle, true), TAG, "panel on failed");

    // Apply saved brightness after the panel is ready.
    apply_brightness_hw(s_brightness);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    static lv_disp_draw_buf_t disp_buf;
    // IMPORTANT: For SPI LCD panel IO (esp_lcd_panel_io_spi), tx buffers must be DMA-capable.
    // On ESP32-S3, PSRAM is *sometimes* DMA-capable, but in practice the SPI path can still reject
    // external buffers. Prefer INTERNAL+DMA buffers (smaller partial buffers) for reliability.
    // With 8MB PSRAM, prefer larger partial buffers to reduce flush calls and improve FPS.
    // We still fall back to smaller heights if DMA-capable allocation isn't available.
    const int desired_heights[] = {
        APP_LCD_V_RES,       // full frame
        APP_LCD_V_RES / 2,
        APP_LCD_V_RES / 3,
        APP_LCD_V_RES / 4,
        APP_LCD_V_RES / 6,
        APP_LCD_V_RES / 8,
        APP_LCD_V_RES / 10,
        80,
        60,
        40,
        30,
        20,
    };

    lv_color_t *buf1 = nullptr;
    lv_color_t *buf2 = nullptr;
    int chosen_height = 0;

    // Wi-Fi (especially SoftAP) needs contiguous INTERNAL heap. Prefer PSRAM DMA buffers for LVGL
    // when available to reduce internal memory pressure.
    const uint32_t caps_try[][2] = {
        {MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, 1},
        {MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, 0},
        {MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, 1}, // fallback
        {MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, 0},
    };

    for (size_t ci = 0; ci < sizeof(caps_try) / sizeof(caps_try[0]) && chosen_height == 0; ci++) {
        const uint32_t caps = caps_try[ci][0];
        const bool is_internal_caps = (caps & MALLOC_CAP_INTERNAL) != 0;
        bool want_double = caps_try[ci][1] != 0;

        // INTERNAL heap is scarce and is also used by I2C/Wi-Fi and other system components.
        // Never double-buffer in INTERNAL, and keep the buffer height small to avoid runtime
        // malloc failures (which can crash I2C command link creation).
        if (is_internal_caps) {
            want_double = false;
        }
        for (size_t hi = 0; hi < sizeof(desired_heights) / sizeof(desired_heights[0]); hi++) {
            int h = desired_heights[hi];
            if (h <= 0) {
                continue;
            }
            if (h > APP_LCD_V_RES) {
                h = APP_LCD_V_RES;
            }

            if (is_internal_caps && h > 40) {
                continue;
            }
            const size_t px = (size_t)APP_LCD_H_RES * (size_t)h;
            const size_t bytes = px * sizeof(lv_color_t);

            buf1 = (lv_color_t *)heap_caps_malloc(bytes, caps);
            if (!buf1) {
                continue;
            }

            // Ensure the buffer is DMA-capable for the SPI panel IO.
            if (!esp_ptr_dma_capable(buf1)) {
                heap_caps_free(buf1);
                buf1 = nullptr;
                continue;
            }
            if (want_double) {
                buf2 = (lv_color_t *)heap_caps_malloc(bytes, caps);
                if (!buf2) {
                    heap_caps_free(buf1);
                    buf1 = nullptr;
                    continue;
                }

                if (!esp_ptr_dma_capable(buf2)) {
                    heap_caps_free(buf2);
                    heap_caps_free(buf1);
                    buf2 = nullptr;
                    buf1 = nullptr;
                    continue;
                }
            } else {
                buf2 = nullptr;
            }

            chosen_height = h;
            const bool is_internal = (caps & MALLOC_CAP_INTERNAL) != 0;
            ESP_LOGI(TAG, "LVGL draw buffers: %s x %dx%d (%s, dma=%d)", want_double ? "2" : "1", APP_LCD_H_RES, chosen_height,
                     is_internal ? "INTERNAL" : "PSRAM", esp_ptr_dma_capable(buf1));
            lv_disp_draw_buf_init(&disp_buf, buf1, buf2, (uint32_t)px);
            break;
        }
    }

    if (chosen_height == 0 || !buf1) {
        ESP_LOGE(TAG, "Failed to allocate any LVGL draw buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    s_disp_drv_ptr = &disp_drv;
    disp_drv.hor_res = APP_LCD_H_RES;  // 368
    disp_drv.ver_res = APP_LCD_V_RES;  // 448
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.drv_update_cb = lvgl_update_cb;
    disp_drv.wait_cb = lvgl_wait_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = s_panel_handle;
    // Keep portrait mode for now
    disp_drv.rotated = LV_DISP_ROT_NONE;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer), TAG, "esp_timer_create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "esp_timer_start_periodic failed");

    ESP_LOGI(TAG, "Init touch controller (FT5x06)");
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(app_i2c_bus(), &tp_io_config, &tp_io_handle), TAG,
                        "esp_lcd_new_panel_io_i2c failed");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = APP_LCD_H_RES,
        .y_max = APP_LCD_V_RES,
        .rst_gpio_num = APP_PIN_NUM_TOUCH_RST,
        .int_gpio_num = APP_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &s_touch), TAG, "touch init failed");

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = s_touch;
    lv_indev_drv_register(&indev_drv);

    s_lvgl_mux = xSemaphoreCreateMutex();
    assert(s_lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, LVGL_TASK_CORE);

    return ESP_OK;
}

void display_lvgl_set_on(bool on)
{
    if (s_panel_handle) {
        esp_lcd_panel_disp_on_off(s_panel_handle, on);
    }
}

void display_lvgl_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) brightness_percent = 100;
    s_brightness = brightness_percent;
    nvs_save_brightness();
    apply_brightness_hw(brightness_percent);
}

uint8_t display_lvgl_get_brightness(void)
{
    return s_brightness;
}
