#include "ui_app_carousel.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "display_lvgl.h"
#include "app_pins.h"
#include "services/app_manager.h"
#include "services/audio_es8311.h"
#include "services/power_axp2101.h"
#include "services/time_service.h"
#include "services/wifi_service.h"
#include "ui_launcher.h"
#include "ui_terminal.h"
#include "ui_media.h"
#include "ui_mp3.h"
#include "ui_fileserver.h"

static const char *TAG = "app_carousel";

static lv_obj_t *s_carousel_screen = nullptr;
static lv_timer_t *s_status_timer = nullptr;
static lv_obj_t *s_status_time = nullptr;
static lv_obj_t *s_status_wifi = nullptr;
static lv_obj_t *s_status_batt = nullptr;

static lv_obj_t *s_center_tile = nullptr;
static lv_obj_t *s_center_icon_img = nullptr;
static lv_obj_t *s_center_icon_sym = nullptr;
static lv_obj_t *s_center_label = nullptr;

static lv_obj_t *s_left_tile = nullptr;
static lv_obj_t *s_left_icon_img = nullptr;
static lv_obj_t *s_left_icon_sym = nullptr;
static lv_obj_t *s_left_label = nullptr;

static lv_obj_t *s_right_tile = nullptr;
static lv_obj_t *s_right_icon_img = nullptr;
static lv_obj_t *s_right_icon_sym = nullptr;
static lv_obj_t *s_right_label = nullptr;

static lv_obj_t *s_page_indicator = nullptr;
static lv_obj_t *s_left_arrow = nullptr;
static lv_obj_t *s_right_arrow = nullptr;

static int s_current_app_index = 0;
static int s_total_apps = 0;

// Built-in app metadata
static app_metadata_t s_builtin_settings;
static app_metadata_t s_builtin_terminal;
static app_metadata_t s_builtin_media;
static app_metadata_t s_builtin_mp3;
static app_metadata_t s_builtin_fileserver;

static const char *builtin_symbol_for(const app_metadata_t *meta)
{
    if (!meta) return nullptr;
    if (strcmp(meta->name, "Settings") == 0) return LV_SYMBOL_SETTINGS;
    if (strcmp(meta->name, "Terminal") == 0) return LV_SYMBOL_EDIT;
    if (strcmp(meta->name, "Media") == 0) return LV_SYMBOL_IMAGE;
    if (strcmp(meta->name, "MP3") == 0) return LV_SYMBOL_AUDIO;
    if (strcmp(meta->name, "FileServer") == 0) return LV_SYMBOL_UPLOAD;
    return nullptr;
}

static void register_builtin_apps(void)
{
    // Register Settings
    memset(&s_builtin_settings, 0, sizeof(app_metadata_t));
    strncpy(s_builtin_settings.name, "Settings", APP_NAME_MAX_LEN - 1);
    strncpy(s_builtin_settings.creator, "System", APP_CREATOR_MAX_LEN - 1);
    s_builtin_settings.category = APP_CATEGORY_SYSTEM;
    s_builtin_settings.size = 0;
    s_builtin_settings.state = APP_STATE_STOPPED;
    s_builtin_settings.storage = APP_STORAGE_FLASH;
    strncpy(s_builtin_settings.path, "<builtin>", sizeof(s_builtin_settings.path) - 1);
    s_builtin_settings.crash_count = 0;
    s_builtin_settings.icon_data = nullptr;
    
    // Register Terminal
    memset(&s_builtin_terminal, 0, sizeof(app_metadata_t));
    strncpy(s_builtin_terminal.name, "Terminal", APP_NAME_MAX_LEN - 1);
    strncpy(s_builtin_terminal.creator, "System", APP_CREATOR_MAX_LEN - 1);
    s_builtin_terminal.category = APP_CATEGORY_UTILITY;
    s_builtin_terminal.size = 0;
    s_builtin_terminal.state = APP_STATE_STOPPED;
    s_builtin_terminal.storage = APP_STORAGE_FLASH;
    strncpy(s_builtin_terminal.path, "<builtin>", sizeof(s_builtin_terminal.path) - 1);
    s_builtin_terminal.crash_count = 0;
    s_builtin_terminal.icon_data = nullptr;

    // Register Media
    memset(&s_builtin_media, 0, sizeof(app_metadata_t));
    strncpy(s_builtin_media.name, "Media", APP_NAME_MAX_LEN - 1);
    strncpy(s_builtin_media.creator, "System", APP_CREATOR_MAX_LEN - 1);
    s_builtin_media.category = APP_CATEGORY_UTILITY;
    s_builtin_media.size = 0;
    s_builtin_media.state = APP_STATE_STOPPED;
    s_builtin_media.storage = APP_STORAGE_FLASH;
    strncpy(s_builtin_media.path, "<builtin>", sizeof(s_builtin_media.path) - 1);
    s_builtin_media.crash_count = 0;
    s_builtin_media.icon_data = nullptr;

    // Register MP3
    memset(&s_builtin_mp3, 0, sizeof(app_metadata_t));
    strncpy(s_builtin_mp3.name, "MP3", APP_NAME_MAX_LEN - 1);
    strncpy(s_builtin_mp3.creator, "System", APP_CREATOR_MAX_LEN - 1);
    s_builtin_mp3.category = APP_CATEGORY_UTILITY;
    s_builtin_mp3.size = 0;
    s_builtin_mp3.state = APP_STATE_STOPPED;
    s_builtin_mp3.storage = APP_STORAGE_FLASH;
    strncpy(s_builtin_mp3.path, "<builtin>", sizeof(s_builtin_mp3.path) - 1);
    s_builtin_mp3.crash_count = 0;
    s_builtin_mp3.icon_data = nullptr;

    // Register FileServer
    memset(&s_builtin_fileserver, 0, sizeof(app_metadata_t));
    strncpy(s_builtin_fileserver.name, "FileServer", APP_NAME_MAX_LEN - 1);
    strncpy(s_builtin_fileserver.creator, "System", APP_CREATOR_MAX_LEN - 1);
    s_builtin_fileserver.category = APP_CATEGORY_UTILITY;
    s_builtin_fileserver.size = 0;
    s_builtin_fileserver.state = APP_STATE_STOPPED;
    s_builtin_fileserver.storage = APP_STORAGE_FLASH;
    strncpy(s_builtin_fileserver.path, "<builtin>", sizeof(s_builtin_fileserver.path) - 1);
    s_builtin_fileserver.crash_count = 0;
    s_builtin_fileserver.icon_data = nullptr;
    
    ESP_LOGI(TAG, "Registered built-in apps: Settings, Terminal, Media, MP3, FileServer");
}

static const app_metadata_t* get_app_at_index(int index)
{
    if (index == 0) return &s_builtin_settings;
    if (index == 1) return &s_builtin_terminal;
    if (index == 2) return &s_builtin_media;
    if (index == 3) return &s_builtin_mp3;
    if (index == 4) return &s_builtin_fileserver;
    
    int app_manager_index = index - 5;
    if (app_manager_index >= 0 && app_manager_index < app_manager_get_app_count()) {
        return app_manager_get_app(app_manager_index);
    }
    
    return nullptr;
}

static void update_carousel_display(void)
{
    auto set_tile = [](lv_obj_t *tile, lv_obj_t *img, lv_obj_t *sym, lv_obj_t *lbl, const app_metadata_t *meta) {
        if (!tile || !lbl || !meta) {
            return;
        }
        lv_label_set_text(lbl, meta->name);

        const char *builtin_sym = builtin_symbol_for(meta);
        const bool has_icon = (meta->icon.data != nullptr && meta->icon.data_size > 0);

        if (img) {
            if (has_icon) {
                lv_img_set_src(img, &meta->icon);
                lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (sym) {
            if (!has_icon && builtin_sym) {
                lv_label_set_text(sym, builtin_sym);
                lv_obj_clear_flag(sym, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(sym, LV_OBJ_FLAG_HIDDEN);
            }
        }
    };

    const app_metadata_t *meta = get_app_at_index(s_current_app_index);
    const app_metadata_t *meta_left = (s_current_app_index > 0) ? get_app_at_index(s_current_app_index - 1) : nullptr;
    const app_metadata_t *meta_right = (s_current_app_index < s_total_apps - 1) ? get_app_at_index(s_current_app_index + 1) : nullptr;
    if (!meta) return;
    
    set_tile(s_center_tile, s_center_icon_img, s_center_icon_sym, s_center_label, meta);
    if (meta_left) {
        lv_obj_clear_flag(s_left_tile, LV_OBJ_FLAG_HIDDEN);
        set_tile(s_left_tile, s_left_icon_img, s_left_icon_sym, s_left_label, meta_left);
    } else if (s_left_tile) {
        lv_obj_add_flag(s_left_tile, LV_OBJ_FLAG_HIDDEN);
    }
    if (meta_right) {
        lv_obj_clear_flag(s_right_tile, LV_OBJ_FLAG_HIDDEN);
        set_tile(s_right_tile, s_right_icon_img, s_right_icon_sym, s_right_label, meta_right);
    } else if (s_right_tile) {
        lv_obj_add_flag(s_right_tile, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update page indicator
    if (s_page_indicator) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d/%d", s_current_app_index + 1, s_total_apps);
        lv_label_set_text(s_page_indicator, buf);
    }
    
    // Update arrow opacity
    if (s_left_arrow) {
        lv_obj_set_style_opa(s_left_arrow, (s_current_app_index > 0) ? LV_OPA_100 : LV_OPA_30, 0);
    }
    if (s_right_arrow) {
        lv_obj_set_style_opa(s_right_arrow, (s_current_app_index < s_total_apps - 1) ? LV_OPA_100 : LV_OPA_30, 0);
    }
}

static void status_timer_cb(lv_timer_t *)
{
    // If carousel screen / widgets were deleted (e.g. another app loaded with delete_prev=true),
    // stop this timer to avoid dereferencing freed LVGL objects.
    if (!s_status_time || !s_status_wifi || !s_status_batt ||
        !lv_obj_is_valid(s_status_time) || !lv_obj_is_valid(s_status_wifi) || !lv_obj_is_valid(s_status_batt)) {
        if (s_status_timer) {
            lv_timer_del(s_status_timer);
            s_status_timer = nullptr;
        }
        s_status_time = nullptr;
        s_status_wifi = nullptr;
        s_status_batt = nullptr;
        return;
    }

    char time_str[16] = {0};
    time_service_format(time_str, sizeof(time_str), nullptr, 0);
    lv_label_set_text(s_status_time, time_str);

    if (wifi_service_is_connected()) {
        lv_obj_set_style_text_color(s_status_wifi, lv_color_white(), 0);
    } else {
        lv_obj_set_style_text_color(s_status_wifi, lv_color_hex(0x888888), 0);
    }

    int batt = power_axp2101_get_battery_percent();
    if (batt < 0) {
        lv_label_set_text(s_status_batt, LV_SYMBOL_BATTERY_EMPTY);
    } else {
        const char *sym = LV_SYMBOL_BATTERY_FULL;
        if (power_axp2101_is_charging()) {
            sym = LV_SYMBOL_CHARGE;
        }
        lv_label_set_text_fmt(s_status_batt, "%s %d%%", sym, batt);
    }
}

static void launch_current_app(void)
{
    const app_metadata_t *meta = get_app_at_index(s_current_app_index);
    if (!meta) return;
    
    ESP_LOGI(TAG, "Launching app: %s", meta->name);
    
    if (s_current_app_index == 0) {
        // Open full Settings menu directly, with Back returning to the carousel.
        ui_launcher_set_home_screen(s_carousel_screen);
        ui_launcher_open_settings();
        return;
    }
    if (s_current_app_index == 1) {
        ui_terminal_init();
        return;
    }
    if (s_current_app_index == 2) {
        ui_media_open();
        return;
    }
    if (s_current_app_index == 3) {
        ui_mp3_open();
        return;
    }
    if (s_current_app_index == 4) {
        ui_fileserver_open();
        return;
    }

    // Filesystem apps exist in the registry, but dynamic loading is not implemented.
    // Show a clear screen instead of appearing to hang.
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, APP_LCD_H_RES, 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_exit = lv_btn_create(header);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(btn_exit, [](lv_event_t *) {
        audio_es8311_play_click();
        if (s_carousel_screen) {
            lv_scr_load_anim(s_carousel_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
        } else {
            ui_app_carousel_init();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, meta->name);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *msg = lv_label_create(scr);
    lv_label_set_text(msg, "This app type isn't supported yet\n(.app code loading is not implemented)");
    lv_obj_set_style_text_color(msg, lv_color_white(), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

    // Keep carousel in memory so Exit can return quickly.
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void ui_app_carousel_open_settings(lv_event_t *)
{
    ui_launcher_set_home_screen(s_carousel_screen);
    ui_launcher_open_settings();
}

lv_obj_t* ui_app_carousel_get_screen(void)
{
    return s_carousel_screen;
}

esp_err_t ui_app_carousel_init(void)
{
    ESP_LOGI(TAG, "=== APP CAROUSEL INIT (SINGLE TILE) ===");
    
    register_builtin_apps();
    s_total_apps = 5 + app_manager_get_app_count();
    s_current_app_index = 0;
    
    ESP_LOGI(TAG, "Total apps: %d", s_total_apps);
    
    // Create screen
    s_carousel_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_carousel_screen, lv_color_hex(0x000000), 0);

    // Ensure the global timer/pointers are cleaned up if this screen is deleted.
    lv_obj_add_event_cb(s_carousel_screen, [](lv_event_t *) {
        if (s_status_timer) {
            lv_timer_del(s_status_timer);
            s_status_timer = nullptr;
        }
        s_status_time = nullptr;
        s_status_wifi = nullptr;
        s_status_batt = nullptr;

        if (s_carousel_screen && lv_obj_is_valid(s_carousel_screen)) {
            // Nothing else to do; LVGL is deleting this object.
        }
        s_carousel_screen = nullptr;
    }, LV_EVENT_DELETE, NULL);
    
    // Status bar (32px tall)
    lv_obj_t *status_bar = lv_obj_create(s_carousel_screen);
    lv_obj_set_size(status_bar, APP_LCD_H_RES, 32);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_status_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_status_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(s_status_wifi, LV_ALIGN_LEFT_MID, 5, 0);

    s_status_time = lv_label_create(status_bar);
    lv_label_set_text(s_status_time, "--:--");
    lv_obj_set_style_text_color(s_status_time, lv_color_white(), 0);
    lv_obj_align(s_status_time, LV_ALIGN_CENTER, 0, 0);

    s_status_batt = lv_label_create(status_bar);
    lv_label_set_text(s_status_batt, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(s_status_batt, lv_color_white(), 0);
    lv_obj_align(s_status_batt, LV_ALIGN_RIGHT_MID, -5, 0);
    
    auto make_tile = [](lv_obj_t *parent, int w, int h, int icon_px, const lv_font_t *sym_font, bool selected,
                        lv_obj_t **out_tile, lv_obj_t **out_img, lv_obj_t **out_sym, lv_obj_t **out_label) {
        lv_obj_t *tile = lv_obj_create(parent);
        lv_obj_set_size(tile, w, h);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_border_color(tile, selected ? lv_color_hex(0x00aaff) : lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(tile, selected ? 2 : 1, 0);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *img = lv_img_create(tile);
        lv_obj_set_size(img, icon_px, icon_px);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *sym = lv_label_create(tile);
        lv_label_set_text(sym, "");
        if (sym_font) {
            lv_obj_set_style_text_font(sym, sym_font, 0);
        }
        lv_obj_set_style_text_color(sym, lv_color_white(), 0);
        lv_obj_align(sym, LV_ALIGN_TOP_MID, 0, 34);
        lv_obj_add_flag(sym, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *lbl = lv_label_create(tile);
        lv_label_set_text(lbl, "App");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

        *out_tile = tile;
        *out_img = img;
        *out_sym = sym;
        *out_label = lbl;
    };

    // Center tile + side previews
    make_tile(s_carousel_screen, 170, 200, 128, &lv_font_montserrat_16, true, &s_center_tile, &s_center_icon_img,
              &s_center_icon_sym, &s_center_label);
    lv_obj_align(s_center_tile, LV_ALIGN_CENTER, 0, -10);

    make_tile(s_carousel_screen, 120, 150, 72, &lv_font_montserrat_12, false, &s_left_tile, &s_left_icon_img,
              &s_left_icon_sym, &s_left_label);
    lv_obj_align(s_left_tile, LV_ALIGN_CENTER, -120, -10);
    lv_obj_set_style_opa(s_left_tile, LV_OPA_60, 0);

    make_tile(s_carousel_screen, 120, 150, 72, &lv_font_montserrat_12, false, &s_right_tile, &s_right_icon_img,
              &s_right_icon_sym, &s_right_label);
    lv_obj_align(s_right_tile, LV_ALIGN_CENTER, 120, -10);
    lv_obj_set_style_opa(s_right_tile, LV_OPA_60, 0);
    
    // Left arrow (< symbol) - using > < text
    s_left_arrow = lv_label_create(s_carousel_screen);
    lv_label_set_text(s_left_arrow, "< <");
    lv_obj_set_style_text_font(s_left_arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_left_arrow, lv_color_white(), 0);
    lv_obj_align(s_left_arrow, LV_ALIGN_LEFT_MID, 15, -20);
    lv_obj_add_flag(s_left_arrow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_left_arrow, [](lv_event_t *) {
        if (s_current_app_index > 0) {
            s_current_app_index--;
            audio_es8311_play_click();
            update_carousel_display();
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Right arrow (> symbol)
    s_right_arrow = lv_label_create(s_carousel_screen);
    lv_label_set_text(s_right_arrow, "> >");
    lv_obj_set_style_text_font(s_right_arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_right_arrow, lv_color_white(), 0);
    lv_obj_align(s_right_arrow, LV_ALIGN_RIGHT_MID, -15, -20);
    lv_obj_add_flag(s_right_arrow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_right_arrow, [](lv_event_t *) {
        if (s_current_app_index < s_total_apps - 1) {
            s_current_app_index++;
            audio_es8311_play_click();
            update_carousel_display();
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Page indicator at bottom (1/2)
    s_page_indicator = lv_label_create(s_carousel_screen);
    lv_label_set_text(s_page_indicator, "1/1");
    lv_obj_set_style_text_color(s_page_indicator, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_page_indicator, &lv_font_montserrat_14, 0);
    lv_obj_align(s_page_indicator, LV_ALIGN_BOTTOM_MID, 0, -50);
    
    // SELECT/OK button at bottom
    lv_obj_t *btn_select = lv_btn_create(s_carousel_screen);
    lv_obj_set_size(btn_select, 100, 40);
    lv_obj_align(btn_select, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(btn_select, lv_color_hex(0x00aa00), 0);
    
    lv_obj_t *lbl_select = lv_label_create(btn_select);
    lv_label_set_text(lbl_select, "SELECT");
    lv_obj_center(lbl_select);
    
    lv_obj_add_event_cb(btn_select, [](lv_event_t *) {
        audio_es8311_play_click();
        launch_current_app();
    }, LV_EVENT_CLICKED, NULL);
    
    // Also allow tap on center tile to launch
    lv_obj_add_flag(s_center_tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_center_tile, [](lv_event_t *) {
        audio_es8311_play_click();
        launch_current_app();
    }, LV_EVENT_CLICKED, NULL);
    
    // Swipe gestures
    lv_obj_add_event_cb(s_carousel_screen, [](lv_event_t *e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT && s_current_app_index < s_total_apps - 1) {
            s_current_app_index++;
            audio_es8311_play_click();
            update_carousel_display();
        } else if (dir == LV_DIR_RIGHT && s_current_app_index > 0) {
            s_current_app_index--;
            audio_es8311_play_click();
            update_carousel_display();
        }
    }, LV_EVENT_GESTURE, NULL);
    
    update_carousel_display();

    // Live status refresh (single instance)
    if (s_status_timer) {
        lv_timer_del(s_status_timer);
        s_status_timer = nullptr;
    }
    s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    status_timer_cb(nullptr);

    // Load and delete previous active screen to avoid leaks when returning to carousel.
    lv_scr_load_anim(s_carousel_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    
    ESP_LOGI(TAG, "=== CAROUSEL READY ===");
    return ESP_OK;
}
