#include "ui_terminal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "app_pins.h"
#include "display_lvgl.h"
#include "ui_app_carousel.h"

static const char *TAG = "terminal";

static lv_obj_t *s_terminal_screen = nullptr;
static lv_obj_t *s_text_area = nullptr;
static lv_obj_t *s_mode_label = nullptr;
static RingbufHandle_t s_log_buffer = nullptr;
static TaskHandle_t s_update_task = nullptr;

static vprintf_like_t s_prev_vprintf = nullptr;

static bool s_ssh_mode = false;
static char s_terminal_buffer[4096];
static size_t s_buffer_len = 0;

// Forward decls
static void term_load_deferred(lv_obj_t *scr, bool auto_del);

static void terminal_stop_task(void)
{
    if (s_update_task) {
        TaskHandle_t t = s_update_task;
        s_update_task = nullptr;
        vTaskDelete(t);
    }
}

static void terminal_stop_capture(void)
{
    // Stop the log pump task first to avoid races while restoring vprintf.
    terminal_stop_task();

    if (s_prev_vprintf) {
        esp_log_set_vprintf(s_prev_vprintf);
        s_prev_vprintf = nullptr;
    }

    if (s_log_buffer) {
        vRingbufferDelete(s_log_buffer);
        s_log_buffer = nullptr;
    }
}

static void terminal_exit_to_carousel_async(void *);

// Capture ESP_LOG output
static int terminal_log_vprintf(const char *fmt, va_list args)
{
    char line_buf[256];
    int len = vsnprintf(line_buf, sizeof(line_buf), fmt, args);
    
    if (s_log_buffer && len > 0) {
        xRingbufferSend(s_log_buffer, line_buf, len, 0);
    }
    
    // Also send to UART
    return vprintf(fmt, args);
}

static void append_to_terminal(const char *text, size_t len)
{
    if (!s_text_area) return;
    
    // Append to buffer
    if (s_buffer_len + len >= sizeof(s_terminal_buffer) - 1) {
        // Buffer full, shift left by half
        size_t half = sizeof(s_terminal_buffer) / 2;
        memmove(s_terminal_buffer, s_terminal_buffer + half, s_buffer_len - half);
        s_buffer_len -= half;
    }
    
    memcpy(s_terminal_buffer + s_buffer_len, text, len);
    s_buffer_len += len;
    s_terminal_buffer[s_buffer_len] = '\0';
    
    // Update text area (LVGL is not thread-safe)
    if (display_lvgl_lock(50)) {
        if (s_text_area && lv_obj_is_valid(s_text_area)) {
            lv_textarea_set_text(s_text_area, s_terminal_buffer);
            lv_obj_scroll_to_y(s_text_area, LV_COORD_MAX, LV_ANIM_OFF);
        }
        display_lvgl_unlock();
    }
}

static void terminal_update_task(void *arg)
{
    size_t item_size;
    char *item;
    
    while (1) {
        item = (char *)xRingbufferReceive(s_log_buffer, &item_size, pdMS_TO_TICKS(100));
        if (item) {
            append_to_terminal(item, item_size);
            vRingbufferReturnItem(s_log_buffer, item);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void terminal_exit_to_carousel_async(void *)
{
    // Ensure LVGL calls happen with our global LVGL lock.
    if (!display_lvgl_lock(200)) {
        // Try again soon
        lv_async_call(terminal_exit_to_carousel_async, nullptr);
        return;
    }

    // Stop background tasks before triggering any screen teardown.
    terminal_stop_capture();

    lv_obj_t *carousel = ui_app_carousel_get_screen();
    if (carousel && lv_obj_is_valid(carousel)) {
        term_load_deferred(carousel, true);
    } else {
        // (Re)create carousel UI
        ui_app_carousel_init();
    }

    display_lvgl_unlock();
}

typedef struct {
    lv_obj_t *scr;
    bool auto_del;
} term_screen_load_req_t;

static void term_load_async_cb(void *p)
{
    term_screen_load_req_t *req = (term_screen_load_req_t *)p;
    if (req && req->scr && lv_obj_is_valid(req->scr)) {
        lv_scr_load_anim(req->scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, req->auto_del);
    }
    if (req) {
        lv_mem_free(req);
    }
}

static void term_load_deferred(lv_obj_t *scr, bool auto_del)
{
    term_screen_load_req_t *req = (term_screen_load_req_t *)lv_mem_alloc(sizeof(term_screen_load_req_t));
    if (!req) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, auto_del);
        return;
    }
    req->scr = scr;
    req->auto_del = auto_del;
    lv_async_call(term_load_async_cb, req);
}

static void switch_mode_event(lv_event_t *e)
{
    s_ssh_mode = !s_ssh_mode;
    
    if (s_mode_label) {
        if (s_ssh_mode) {
            lv_label_set_text(s_mode_label, "Mode: SSH (not implemented)");
            lv_obj_set_style_text_color(s_mode_label, lv_color_hex(0xFFAA00), 0);
        } else {
            lv_label_set_text(s_mode_label, "Mode: Serial Output");
            lv_obj_set_style_text_color(s_mode_label, lv_color_hex(0x00FF00), 0);
        }
    }
    
    ESP_LOGI(TAG, "Switched to %s mode", s_ssh_mode ? "SSH" : "Serial");
}

static void clear_terminal_event(lv_event_t *e)
{
    s_buffer_len = 0;
    s_terminal_buffer[0] = '\0';
    if (display_lvgl_lock(50)) {
        if (s_text_area && lv_obj_is_valid(s_text_area)) {
            lv_textarea_set_text(s_text_area, "");
        }
        display_lvgl_unlock();
    }
    ESP_LOGI(TAG, "Terminal cleared");
}

esp_err_t ui_terminal_init(void)
{
    ESP_LOGI(TAG, "Initializing terminal UI");

    // If already initialized, just open it.
    if (s_terminal_screen && lv_obj_is_valid(s_terminal_screen)) {
        term_load_deferred(s_terminal_screen, true);
        return ESP_OK;
    }
    
    // Create ring buffer for log capture
    if (!s_log_buffer) {
        s_log_buffer = xRingbufferCreate(8192, RINGBUF_TYPE_NOSPLIT);
        if (!s_log_buffer) {
            ESP_LOGE(TAG, "Failed to create ring buffer");
            return ESP_FAIL;
        }
        
        // Redirect ESP_LOG to our buffer
        s_prev_vprintf = esp_log_set_vprintf(terminal_log_vprintf);
    }
    
    // Create terminal screen
    s_terminal_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_terminal_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(s_terminal_screen, 0, 0);
    
    // Header bar
    lv_obj_t *header = lv_obj_create(s_terminal_screen);
    lv_obj_set_size(header, APP_LCD_H_RES, 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Exit button
    lv_obj_t *btn_exit = lv_btn_create(header);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(btn_exit, [](lv_event_t *) {
        // Defer: avoid deleting/loading screens inside event callback.
        lv_async_call(terminal_exit_to_carousel_async, nullptr);
    }, LV_EVENT_CLICKED, NULL);

    // If the terminal screen is deleted (e.g., replaced by carousel), stop its worker task and clear pointers.
    lv_obj_add_event_cb(s_terminal_screen,
                        [](lv_event_t *) {
                            terminal_stop_capture();
                            s_terminal_screen = nullptr;
                            s_text_area = nullptr;
                            s_mode_label = nullptr;
                            s_buffer_len = 0;
                            s_terminal_buffer[0] = '\0';
                        },
                        LV_EVENT_DELETE, NULL);
    
    // Title
    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Terminal");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);
    
    // Mode switch button
    lv_obj_t *btn_mode = lv_btn_create(header);
    lv_obj_set_size(btn_mode, 60, 32);
    lv_obj_align(btn_mode, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_t *lbl_mode = lv_label_create(btn_mode);
    lv_label_set_text(lbl_mode, "Mode");
    lv_obj_center(lbl_mode);
    lv_obj_add_event_cb(btn_mode, switch_mode_event, LV_EVENT_CLICKED, NULL);
    
    // Mode indicator
    s_mode_label = lv_label_create(s_terminal_screen);
    lv_label_set_text(s_mode_label, "Mode: Serial Output");
    lv_obj_set_style_text_color(s_mode_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(s_mode_label, LV_ALIGN_TOP_MID, 0, 48);
    
    // Text area for terminal output
    s_text_area = lv_textarea_create(s_terminal_screen);
    lv_obj_set_size(s_text_area, APP_LCD_H_RES - 8, APP_LCD_V_RES - 120);
    lv_obj_align(s_text_area, LV_ALIGN_TOP_MID, 0, 70);
    lv_textarea_set_text(s_text_area, "");
    lv_obj_set_style_bg_color(s_text_area, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_text_color(s_text_area, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_text_font(s_text_area, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(s_text_area, LV_OBJ_FLAG_CLICKABLE);
    
    // Clear button at bottom
    lv_obj_t *btn_clear = lv_btn_create(s_terminal_screen);
    lv_obj_set_size(btn_clear, 100, 40);
    lv_obj_align(btn_clear, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *lbl_clear = lv_label_create(btn_clear);
    lv_label_set_text(lbl_clear, "Clear");
    lv_obj_center(lbl_clear);
    lv_obj_add_event_cb(btn_clear, clear_terminal_event, LV_EVENT_CLICKED, NULL);
    
    // Load screen
    term_load_deferred(s_terminal_screen, true);
    
    // Start update task
    if (!s_update_task) {
    #if CONFIG_FREERTOS_UNICORE
        xTaskCreate(terminal_update_task, "terminal_update", 4096, NULL, 5, &s_update_task);
    #else
        // Keep log pump off the LVGL core when possible.
        xTaskCreatePinnedToCore(terminal_update_task, "terminal_update", 4096, NULL, 5, &s_update_task, 1);
    #endif
    }
    
    ESP_LOGI(TAG, "Terminal UI initialized");
    
    return ESP_OK;
}

esp_err_t ui_terminal_open(void)
{
    return ui_terminal_init();
}
