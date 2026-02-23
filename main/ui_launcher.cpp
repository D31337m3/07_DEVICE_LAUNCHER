#include "ui_launcher.h"

#include <stdio.h>

#include "lvgl.h"
#include "esp_wifi.h"

#include "display_lvgl.h"
#include "app_pins.h"
#include "ui_app_carousel.h"
#include "ui_radio.h"

#include "services/power_axp2101.h"
#include "services/power_manager.h"
#include "services/time_service.h"
#include "services/wifi_service.h"
#include "services/ble_service.h"
#include "services/imu_qmi8658.h"
#include "services/audio_es8311.h"
#include "services/sdcard_service.h"
#include "services/ota_service.h"

static lv_obj_t *s_label_time = nullptr;
static lv_obj_t *s_label_date = nullptr;
static lv_obj_t *s_label_notif = nullptr;
static lv_obj_t *s_bar_batt = nullptr;

static lv_obj_t *s_home = nullptr;
static lv_obj_t *s_keyboard = nullptr;

static inline void ui_click(void)
{
    audio_es8311_play_click();
}

typedef struct {
    lv_obj_t *scr;
    bool auto_del;
} ui_screen_load_req_t;

static void ui_screen_load_async_cb(void *p)
{
    ui_screen_load_req_t *req = (ui_screen_load_req_t *)p;
    if (req && req->scr && lv_obj_is_valid(req->scr)) {
        lv_scr_load_anim(req->scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, req->auto_del);
    }
    if (req) {
        lv_mem_free(req);
    }
}

static inline void ui_load_screen_deferred(lv_obj_t *scr, bool auto_del)
{
    // Never delete/load the active screen inside its own event callback.
    ui_screen_load_req_t *req = (ui_screen_load_req_t *)lv_mem_alloc(sizeof(ui_screen_load_req_t));
    if (!req) {
        // Fallback: best-effort immediate load.
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, auto_del);
        return;
    }
    req->scr = scr;
    req->auto_del = auto_del;
    lv_async_call(ui_screen_load_async_cb, req);
}

static inline void ui_push_screen(lv_obj_t *scr)
{
    // Keep the previous screen so Back can return to it.
    ui_load_screen_deferred(scr, false);
}

static inline void ui_replace_screen(lv_obj_t *scr)
{
    // Delete the previous screen to avoid leaking memory on navigation.
    ui_load_screen_deferred(scr, true);
}

static lv_obj_t *make_app_screen(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, 44);
    lv_obj_set_style_pad_all(hdr, 6, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 60, 32);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn_back);
    lv_label_set_text(lbl, "Back");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *) { 
        ui_click();
        if (s_keyboard) {
            lv_obj_del(s_keyboard);
            s_keyboard = nullptr;
        }
        if (s_home) {
            ui_replace_screen(s_home);
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Exit button to return to carousel
    lv_obj_t *btn_exit = lv_btn_create(hdr);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(btn_exit, [](lv_event_t *) { 
        ui_click();
        lv_obj_t *carousel = ui_app_carousel_get_screen();
        if (carousel) {
            ui_replace_screen(carousel);
        } else {
            extern esp_err_t ui_app_carousel_init(void);
            ui_app_carousel_init();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

static inline void set_content_area(lv_obj_t *cont)
{
    // Header is 44px tall, plus some spacing.
    constexpr int kTop = 52;
    lv_obj_set_size(cont, APP_LCD_H_RES, APP_LCD_V_RES - kTop);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, kTop);
}

static void textarea_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        if (s_keyboard == nullptr) {
            lv_obj_t *scr = lv_scr_act();
            s_keyboard = lv_keyboard_create(scr);
            lv_obj_set_size(s_keyboard, APP_LCD_H_RES, APP_LCD_V_RES / 2);
            lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
        lv_keyboard_set_textarea(s_keyboard, ta);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (s_keyboard) {
            lv_obj_del(s_keyboard);
            s_keyboard = nullptr;
        }
    }
}

static void add_list_item(lv_obj_t *list, const char *name, void (*on_open)(void))
{
    lv_obj_t *btn = lv_list_add_btn(list, NULL, name);
    lv_obj_add_event_cb(btn,
                        [](lv_event_t *e) {
                            ui_click();
                            auto fn = (void (*)())lv_event_get_user_data(e);
                            if (fn) fn();
                        },
                        LV_EVENT_CLICKED, (void *)on_open);
}

// Forward declarations
static void open_display_settings();
static void open_wifi();
static void open_imu();
static void open_audio();
static void open_sd();
static void open_ota();
static void open_power();

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *ta_ssid;
    lv_obj_t *ta_pass;
    lv_obj_t *lbl_status;
    lv_obj_t *list;
} wifi_ui_refs_t;

static void wifi_ui_start_scan(wifi_ui_refs_t *r);

static bool s_wifi_ui_scan_running = false;

static void open_settings()
{
    lv_obj_t *scr = make_app_screen("Settings");

    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(list, 0, 0);

    lv_obj_t *btn_display = lv_list_add_btn(list, NULL, "Display");
    lv_obj_add_event_cb(btn_display, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_display_settings();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_wifi = lv_list_add_btn(list, NULL, "WiFi");
    lv_obj_add_event_cb(btn_wifi, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_wifi();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ble = lv_list_add_btn(list, NULL, "Bluetooth");
    lv_obj_add_event_cb(btn_ble,
                        [](lv_event_t *) {
                            ui_click();
                            bool en = !ble_service_is_enabled();
                            ble_service_set_enabled(en);
                        },
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_imu = lv_list_add_btn(list, NULL, "IMU");
    lv_obj_add_event_cb(btn_imu, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_imu();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_audio = lv_list_add_btn(list, NULL, "Audio");
    lv_obj_add_event_cb(btn_audio, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_audio();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_radio = lv_list_add_btn(list, NULL, "Internet Radio");
    lv_obj_add_event_cb(btn_radio, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        ui_radio_open();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_sd = lv_list_add_btn(list, NULL, "SD Card");
    lv_obj_add_event_cb(btn_sd, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_sd();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ota = lv_list_add_btn(list, NULL, "OTA Update");
    lv_obj_add_event_cb(btn_ota, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_ota();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_power = lv_list_add_btn(list, NULL, "Power");
    lv_obj_add_event_cb(btn_power, [](lv_event_t *) {
        ui_click();
        ui_launcher_set_home_screen(lv_scr_act());
        open_power();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_sntp = lv_list_add_btn(list, NULL, "Sync time (SNTP)");
    lv_obj_add_event_cb(btn_sntp, [](lv_event_t *) {
        ui_click();
        time_service_start_sntp();
    }, LV_EVENT_CLICKED, NULL);

    ui_push_screen(scr);
}

static void open_display_settings()
{
    lv_obj_t *scr = make_app_screen("Display");

    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    // Brightness label and slider
    lv_obj_t *lbl_brightness = lv_label_create(cont);
    lv_label_set_text_fmt(lbl_brightness, "Brightness: %d%%", display_lvgl_get_brightness());
    lv_obj_align(lbl_brightness, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *slider_brightness = lv_slider_create(cont);
    lv_slider_set_range(slider_brightness, 10, 100);
    lv_slider_set_value(slider_brightness, display_lvgl_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_width(slider_brightness, lv_pct(100));
    lv_obj_align_to(slider_brightness, lbl_brightness, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_add_event_cb(slider_brightness,
                        [](lv_event_t *e) {
                            ui_click();
                            lv_obj_t *slider = lv_event_get_target(e);
                            lv_obj_t *cont = lv_obj_get_parent(slider);
                            lv_obj_t *lbl = lv_obj_get_child(cont, 0);
                            int value = lv_slider_get_value(slider);
                            display_lvgl_set_brightness(value);
                            lv_label_set_text_fmt(lbl, "Brightness: %d%%", value);
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "Idle / sleep timers are in Settings > Power");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_align_to(hint, slider_brightness, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);

    ui_push_screen(scr);
}

static uint16_t dropdown_select_for_seconds(uint32_t sec, const uint32_t *values, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        if (values[i] == sec) {
            return i;
        }
    }
    return 0;
}

static void open_power()
{
    lv_obj_t *scr = make_app_screen("Power");

    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    // UI sounds
    lv_obj_t *cb_sounds = lv_checkbox_create(cont);
    lv_checkbox_set_text(cb_sounds, "UI click sounds");
    if (audio_es8311_get_ui_sounds_enabled()) {
        lv_obj_add_state(cb_sounds, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(cb_sounds,
                        [](lv_event_t *e) {
                            lv_obj_t *cb = lv_event_get_target(e);
                            const bool en = lv_obj_has_state(cb, LV_STATE_CHECKED);
                            audio_es8311_set_ui_sounds_enabled(en);
                            ui_click();
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Idle display off
    lv_obj_t *lbl_idle = lv_label_create(cont);
    lv_label_set_text(lbl_idle, "Idle: turn display off");

    lv_obj_t *dd_idle = lv_dropdown_create(cont);
    lv_dropdown_set_options(dd_idle, "Off\n15 sec\n30 sec\n1 min\n2 min\n5 min\n10 min");
    static const uint32_t kIdleValues[] = {0, 15, 30, 60, 120, 300, 600};
    lv_dropdown_set_selected(dd_idle,
                             dropdown_select_for_seconds(power_manager_get_idle_timeout_sec(), kIdleValues,
                                                         (uint16_t)(sizeof(kIdleValues) / sizeof(kIdleValues[0]))));
    lv_obj_set_width(dd_idle, lv_pct(100));
    lv_obj_add_event_cb(dd_idle,
                        [](lv_event_t *e) {
                            ui_click();
                            uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                            if (sel >= (sizeof(kIdleValues) / sizeof(kIdleValues[0]))) sel = 0;
                            power_manager_set_idle_timeout_sec(kIdleValues[sel]);
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Sleep
    lv_obj_t *lbl_sleep = lv_label_create(cont);
    lv_label_set_text(lbl_sleep, "Sleep after inactivity");

    lv_obj_t *dd_sleep = lv_dropdown_create(cont);
    lv_dropdown_set_options(dd_sleep, "Off\n1 min\n2 min\n5 min\n10 min\n30 min");
    static const uint32_t kSleepValues[] = {0, 60, 120, 300, 600, 1800};
    lv_dropdown_set_selected(dd_sleep,
                             dropdown_select_for_seconds(power_manager_get_sleep_timeout_sec(), kSleepValues,
                                                         (uint16_t)(sizeof(kSleepValues) / sizeof(kSleepValues[0]))));
    lv_obj_set_width(dd_sleep, lv_pct(100));
    lv_obj_add_event_cb(dd_sleep,
                        [](lv_event_t *e) {
                            ui_click();
                            uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
                            if (sel >= (sizeof(kSleepValues) / sizeof(kSleepValues[0]))) sel = 0;
                            power_manager_set_sleep_timeout_sec(kSleepValues[sel]);
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Actions
    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_action_btn = [](lv_obj_t *parent, const char *text, lv_color_t bg, void (*fn)(void)) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 110, 42);
        lv_obj_set_style_bg_color(btn, bg, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn,
                            [](lv_event_t *e) {
                                ui_click();
                                auto f = (void (*)())lv_event_get_user_data(e);
                                // Run in a one-shot timer so we don't block the click handler.
                                lv_timer_create(
                                    [](lv_timer_t *t) {
                                        auto f2 = (void (*)())t->user_data;
                                        if (f2) f2();
                                        lv_timer_del(t);
                                    },
                                    50, (void *)f);
                            },
                            LV_EVENT_CLICKED, (void *)fn);
        return btn;
    };

    make_action_btn(row, "Sleep", lv_color_hex(0x444444), power_manager_sleep_now);
    make_action_btn(row, "Reboot", lv_color_hex(0x0066AA), power_manager_reboot_now);
    make_action_btn(row, "Shutdown", lv_color_hex(0xAA0000), power_manager_shutdown_now);

    ui_push_screen(scr);
}

static void open_wifi()
{
    lv_obj_t *scr = make_app_screen("WiFi");

    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    wifi_ui_refs_t *refs = (wifi_ui_refs_t *)lv_mem_alloc(sizeof(wifi_ui_refs_t));
    if (refs) {
        refs->screen = scr;
        refs->ta_ssid = nullptr;
        refs->ta_pass = nullptr;
        refs->lbl_status = nullptr;
        refs->list = nullptr;
        lv_obj_add_event_cb(scr,
                            [](lv_event_t *e) {
                                wifi_ui_refs_t *r = (wifi_ui_refs_t *)lv_event_get_user_data(e);
                                if (r) lv_mem_free(r);
                            },
                            LV_EVENT_DELETE, refs);
    }

    lv_obj_t *ta_ssid = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "SSID");
    lv_obj_set_width(ta_ssid, lv_pct(100));
    lv_obj_add_event_cb(ta_ssid, textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *ta_pass = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_placeholder_text(ta_pass, "Password");
    lv_obj_set_width(ta_pass, lv_pct(100));
    lv_obj_align_to(ta_pass, ta_ssid, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_add_event_cb(ta_pass, textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_status = lv_label_create(cont);
    lv_label_set_text(lbl_status, "Status: -");
    lv_obj_align_to(lbl_status, ta_pass, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    // Prefill saved credentials (if any)
    {
        char ssid[33] = {0};
        char pass[65] = {0};
        if (wifi_service_get_saved_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
            lv_textarea_set_text(ta_ssid, ssid);
            lv_textarea_set_text(ta_pass, pass);
        }
    }

    if (refs) {
        refs->ta_ssid = ta_ssid;
        refs->ta_pass = ta_pass;
        refs->lbl_status = lbl_status;
    }

    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 100, 32);
    lv_obj_align_to(btn, lbl_status, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Connect");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e) {
            ui_click();
            wifi_ui_refs_t *r = (wifi_ui_refs_t *)lv_event_get_user_data(e);
            if (!r || !r->ta_ssid || !r->ta_pass || !lv_obj_is_valid(r->ta_ssid) || !lv_obj_is_valid(r->ta_pass)) return;
            wifi_service_connect(lv_textarea_get_text(r->ta_ssid), lv_textarea_get_text(r->ta_pass));
        },
        LV_EVENT_CLICKED, refs);

    // Scan button
    lv_obj_t *btn_scan = lv_btn_create(cont);
    lv_obj_set_size(btn_scan, 100, 32);
    lv_obj_align_to(btn_scan, btn, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_t *lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "Scan");
    lv_obj_center(lbl_scan);
    lv_obj_add_event_cb(
        btn_scan,
        [](lv_event_t *e) {
            ui_click();
            wifi_ui_refs_t *r = (wifi_ui_refs_t *)lv_event_get_user_data(e);
            if (!r) return;
            wifi_ui_start_scan(r);
        },
        LV_EVENT_CLICKED, refs);

    // Available networks label
    lv_obj_t *lbl_networks = lv_label_create(cont);
    lv_label_set_text(lbl_networks, "Available Networks:");
    lv_obj_align_to(lbl_networks, btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    // Network list
    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, 140);
    lv_obj_align_to(list, lbl_networks, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0a0a0a), 0);
    // Ensure list item text is visible (some themes default to dark text).
    lv_obj_set_style_text_color(list, lv_color_white(), LV_PART_ITEMS);
    if (refs) {
        refs->list = list;
    }

    // Initial scan
    wifi_ui_start_scan(refs);

    // Update status via timer
    lv_timer_create(
        [](lv_timer_t *t) {
            lv_obj_t *lbl = (lv_obj_t *)t->user_data;
            if (!lbl || !lv_obj_is_valid(lbl)) {
                lv_timer_del(t);
                return;
            }
            char ip[32];
            wifi_service_get_ip(ip, sizeof(ip));
            if (wifi_service_is_connected()) {
                lv_label_set_text_fmt(lbl, "Status: connected (%s)", ip);
            } else {
                lv_label_set_text(lbl, "Status: disconnected");
            }
        },
        1000, lbl_status);

    ui_push_screen(scr);
}

static void open_imu()
{
    lv_obj_t *scr = make_app_screen("IMU");
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 60);
    lv_label_set_text(lbl, "-");

    lv_timer_create(
        [](lv_timer_t *t) {
            float ax, ay, az, gx, gy, gz;
            if (imu_qmi8658_read(&ax, &ay, &az, &gx, &gy, &gz)) {
                lv_label_set_text_fmt((lv_obj_t *)t->user_data,
                                      "ACC: %.2f %.2f %.2f\nGYR: %.2f %.2f %.2f",
                                      ax, ay, az, gx, gy, gz);
            }
        },
        200, lbl);

    ui_push_screen(scr);
}

static void open_audio()
{
    lv_obj_t *scr = make_app_screen("Audio");

    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    // Output enable/disable
    lv_obj_t *cb_disable = lv_checkbox_create(cont);
    lv_checkbox_set_text(cb_disable, "Disable audio output");
    if (!audio_es8311_get_enabled()) {
        lv_obj_add_state(cb_disable, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(cb_disable,
                        [](lv_event_t *e) {
                            lv_obj_t *cb = lv_event_get_target(e);
                            const bool disabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
                            audio_es8311_set_enabled(!disabled);
                            ui_click();
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Volume
    lv_obj_t *lbl_vol = lv_label_create(cont);
    lv_label_set_text_fmt(lbl_vol, "Volume: %d%%", audio_es8311_get_volume());

    lv_obj_t *slider_vol = lv_slider_create(cont);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_slider_set_value(slider_vol, audio_es8311_get_volume(), LV_ANIM_OFF);
    lv_obj_set_width(slider_vol, lv_pct(100));
    lv_obj_add_event_cb(slider_vol,
                        [](lv_event_t *e) {
                            lv_obj_t *slider = lv_event_get_target(e);
                            lv_obj_t *cont = lv_obj_get_parent(slider);
                            lv_obj_t *lbl = lv_obj_get_child(cont, 1); // volume label after disable checkbox
                            int value = lv_slider_get_value(slider);
                            audio_es8311_set_volume(value);
                            lv_label_set_text_fmt(lbl, "Volume: %d%%", value);
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Mute
    lv_obj_t *cb_mute = lv_checkbox_create(cont);
    lv_checkbox_set_text(cb_mute, "Mute");
    if (audio_es8311_get_muted()) {
        lv_obj_add_state(cb_mute, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(cb_mute,
                        [](lv_event_t *e) {
                            lv_obj_t *cb = lv_event_get_target(e);
                            const bool muted = lv_obj_has_state(cb, LV_STATE_CHECKED);
                            audio_es8311_set_muted(muted);
                            ui_click();
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Mic gain
    lv_obj_t *lbl_mic = lv_label_create(cont);
    lv_label_set_text_fmt(lbl_mic, "Mic gain: %d%%", audio_es8311_get_mic_gain());

    lv_obj_t *slider_mic = lv_slider_create(cont);
    lv_slider_set_range(slider_mic, 0, 100);
    lv_slider_set_value(slider_mic, audio_es8311_get_mic_gain(), LV_ANIM_OFF);
    lv_obj_set_width(slider_mic, lv_pct(100));
    lv_obj_add_event_cb(slider_mic,
                        [](lv_event_t *e) {
                            lv_obj_t *slider = lv_event_get_target(e);
                            lv_obj_t *cont = lv_obj_get_parent(slider);
                            // mic label is the child right before mic slider
                            lv_obj_t *lbl = lv_obj_get_child(cont, 4);
                            int value = lv_slider_get_value(slider);
                            audio_es8311_set_mic_gain(value);
                            lv_label_set_text_fmt(lbl, "Mic gain: %d%%", value);
                        },
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Mic input level gauge
    lv_obj_t *lbl_lvl = lv_label_create(cont);
    lv_label_set_text(lbl_lvl, "Mic input level");

    lv_obj_t *bar = lv_bar_create(cont);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 18);

    lv_timer_create(
        [](lv_timer_t *t) {
            lv_obj_t *bar = (lv_obj_t *)t->user_data;
            if (!lv_obj_is_valid(bar)) {
                lv_timer_del(t);
                return;
            }
            const int level = audio_es8311_get_mic_level();
            lv_bar_set_value(bar, level, LV_ANIM_OFF);
        },
        100, bar);

    // Test beep
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 140, 44);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Test beep");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn,
                        [](lv_event_t *) {
                            ui_click();
                            audio_es8311_play_beep();
                        },
                        LV_EVENT_CLICKED, NULL);

    ui_push_screen(scr);
}

static void open_sd()
{
    lv_obj_t *scr = make_app_screen("SD Card");

    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl, "Mounting...");

    ui_push_screen(scr);

    // Live progress text while mount task runs.
    typedef struct {
        lv_obj_t *screen;
        lv_obj_t *label;
        uint32_t start_ms;
    } sd_progress_ctx_t;

    sd_progress_ctx_t *pctx = (sd_progress_ctx_t *)lv_mem_alloc(sizeof(sd_progress_ctx_t));
    if (pctx) {
        pctx->screen = scr;
        pctx->label = lbl;
        pctx->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        lv_timer_create(
            [](lv_timer_t *t) {
                sd_progress_ctx_t *ctx = (sd_progress_ctx_t *)t->user_data;
                if (!ctx || !ctx->screen || !ctx->label || !lv_obj_is_valid(ctx->screen) || !lv_obj_is_valid(ctx->label) ||
                    lv_scr_act() != ctx->screen) {
                    if (ctx) lv_mem_free(ctx);
                    lv_timer_del(t);
                    return;
                }
                uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                uint32_t el = (now_ms - ctx->start_ms) / 1000U;
                lv_label_set_text_fmt(ctx->label, "Mounting... (%lus)\n%s", (unsigned long)el, sdcard_service_last_status());
            },
            250,
            pctx);
    }

    typedef struct {
        lv_obj_t *screen;
        lv_obj_t *label;
    } sd_ui_ctx_t;

    sd_ui_ctx_t *ctx = (sd_ui_ctx_t *)lv_mem_alloc(sizeof(sd_ui_ctx_t));
    if (!ctx) {
        lv_label_set_text(lbl, "Out of memory");
        return;
    }
    ctx->screen = scr;
    ctx->label = lbl;

    auto worker = [](void *arg) {
        sd_ui_ctx_t *ctx = (sd_ui_ctx_t *)arg;
        const esp_err_t ret = sdcard_service_mount();

        // Update UI in LVGL context
        lv_async_call(
            [](void *p) {
                sd_ui_ctx_t *ctx = (sd_ui_ctx_t *)p;
                if (ctx && ctx->screen && ctx->label && lv_obj_is_valid(ctx->screen) && lv_obj_is_valid(ctx->label) &&
                    lv_scr_act() == ctx->screen) {
                    if (sdcard_service_is_mounted()) {
                        lv_label_set_text_fmt(ctx->label, "Mounted:\n%s", sdcard_service_mount_point());
                    } else {
                        lv_label_set_text_fmt(ctx->label, "Mount failed:\n%s", esp_err_to_name(sdcard_service_last_error()));
                    }
                }
                if (ctx) {
                    lv_mem_free(ctx);
                }
            },
            ctx);

        (void)ret;
        vTaskDelete(NULL);
    };

    xTaskCreate(worker, "sd_mount", 4096, ctx, 2, NULL);
}

static void open_ota()
{
    lv_obj_t *scr = make_app_screen("OTA");

    lv_obj_t *ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "Firmware URL (http(s)://...)");
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(ta, textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Update");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn,
                        [](lv_event_t *e) {
                            lv_obj_t *scr = lv_scr_act();
                            lv_obj_t *ta = lv_obj_get_child(scr, 1); // textarea after header
                            ota_service_start_from_url(lv_textarea_get_text(ta));
                        },
                        LV_EVENT_CLICKED, NULL);

    ui_push_screen(scr);
}

esp_err_t ui_launcher_init(void)
{
    s_home = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(s_home, 0, 0);

    // Status bar
    lv_obj_t *bar = lv_obj_create(s_home);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 32);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 4, 0);

    s_label_notif = lv_label_create(bar);
    lv_label_set_text(s_label_notif, "-");
    lv_obj_align(s_label_notif, LV_ALIGN_LEFT_MID, 0, 0);

    s_label_time = lv_label_create(bar);
    lv_label_set_text(s_label_time, "--:--");
    lv_obj_align(s_label_time, LV_ALIGN_CENTER, 0, -6);

    s_label_date = lv_label_create(bar);
    lv_label_set_text(s_label_date, "----/--/--");
    lv_obj_align(s_label_date, LV_ALIGN_CENTER, 0, 8);

    s_bar_batt = lv_bar_create(bar);
    lv_obj_set_size(s_bar_batt, 70, 18);
    lv_bar_set_range(s_bar_batt, 0, 100);
    lv_obj_align(s_bar_batt, LV_ALIGN_RIGHT_MID, 0, 0);

    // App list
    lv_obj_t *list = lv_list_create(s_home);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, APP_LCD_V_RES - 32);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

    add_list_item(list, "Settings", open_settings);
    // Future app placeholders can go here

    ui_replace_screen(s_home);

    // 1Hz status bar refresh
    lv_timer_create([](lv_timer_t *) { ui_launcher_status_update(); }, 1000, NULL);
    ui_launcher_status_update();
    return ESP_OK;
}

void ui_launcher_set_home_screen(lv_obj_t *home_screen)
{
    if (home_screen) {
        s_home = home_screen;
    }
}

esp_err_t ui_launcher_open_settings(void)
{
    open_settings();
    return ESP_OK;
}

void ui_launcher_status_update(void)
{
    char time_str[16] = {0};
    char date_str[16] = {0};
    time_service_format(time_str, sizeof(time_str), date_str, sizeof(date_str));
    lv_label_set_text(s_label_time, time_str);
    lv_label_set_text(s_label_date, date_str);

    int batt = power_axp2101_get_battery_percent();
    if (batt >= 0) {
        lv_bar_set_value(s_bar_batt, batt, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(s_bar_batt, 0, LV_ANIM_OFF);
    }

    // Notifications: letters for basic system states
    // W = WiFi connected, B = BLE enabled, S = SD mounted, C = Charging
    char notif[8] = {0};
    int idx = 0;
    if (wifi_service_is_connected()) notif[idx++] = 'W';
    if (ble_service_is_enabled()) notif[idx++] = 'B';
    if (sdcard_service_is_mounted()) notif[idx++] = 'S';
    if (power_axp2101_is_charging()) notif[idx++] = 'C';
    if (idx == 0) notif[idx++] = '-';
    notif[idx] = '\0';
    lv_label_set_text(s_label_notif, notif);
}

static void wifi_ui_start_scan(wifi_ui_refs_t *r)
{
    if (!r || !r->screen || !r->list) return;
    if (!lv_obj_is_valid(r->screen) || !lv_obj_is_valid(r->list)) return;

    if (s_wifi_ui_scan_running) {
        return;
    }
    s_wifi_ui_scan_running = true;

    lv_obj_clean(r->list);
    lv_obj_t *b = lv_list_add_btn(r->list, NULL, "Scanning...");
    lv_obj_add_state(b, LV_STATE_DISABLED);

    typedef struct {
        wifi_ui_refs_t *refs;
    } wifi_scan_req_t;

    wifi_scan_req_t *req = (wifi_scan_req_t *)lv_mem_alloc(sizeof(wifi_scan_req_t));
    if (!req) return;
    req->refs = r;

    auto worker = [](void *arg) {
        wifi_scan_req_t *req = (wifi_scan_req_t *)arg;
        wifi_ap_record_simple_t ap_list[12];
        uint16_t ap_count = 0;
        esp_err_t err = wifi_service_scan(ap_list, &ap_count, (uint16_t)(sizeof(ap_list) / sizeof(ap_list[0])));

        typedef struct {
            wifi_ui_refs_t *refs;
            wifi_ap_record_simple_t aps[12];
            uint16_t count;
            esp_err_t err;
        } wifi_scan_done_t;

        wifi_scan_done_t *done = (wifi_scan_done_t *)lv_mem_alloc(sizeof(wifi_scan_done_t));
        if (done) {
            done->refs = req ? req->refs : nullptr;
            done->count = ap_count;
            done->err = err;
            memcpy(done->aps, ap_list, sizeof(ap_list));
        }

        lv_async_call(
            [](void *p) {
                wifi_scan_done_t *done = (wifi_scan_done_t *)p;
                if (!done) return;

                wifi_ui_refs_t *r = done->refs;
                if (!r || !r->screen || !r->list || !lv_obj_is_valid(r->screen) || !lv_obj_is_valid(r->list) ||
                    lv_scr_act() != r->screen) {
                    s_wifi_ui_scan_running = false;
                    lv_mem_free(done);
                    return;
                }

                lv_obj_clean(r->list);

                if (done->err != ESP_OK) {
                    lv_obj_t *b = lv_list_add_btn(r->list, NULL, "Scan failed");
                    lv_obj_add_state(b, LV_STATE_DISABLED);
                    lv_obj_t *b2 = lv_list_add_btn(r->list, NULL, esp_err_to_name(done->err));
                    lv_obj_add_state(b2, LV_STATE_DISABLED);
                    s_wifi_ui_scan_running = false;
                    lv_mem_free(done);
                    return;
                }

                if (done->count == 0) {
                    lv_obj_t *b = lv_list_add_btn(r->list, NULL, "No networks found");
                    lv_obj_add_state(b, LV_STATE_DISABLED);
                    s_wifi_ui_scan_running = false;
                    lv_mem_free(done);
                    return;
                }

                typedef struct {
                    wifi_ui_refs_t *refs;
                    char ssid[33];
                } wifi_ap_ud_t;

                for (uint16_t i = 0; i < done->count; i++) {
                    char label[72];
                    const char *auth = (done->aps[i].authmode == WIFI_AUTH_OPEN) ? "OPEN" : "LOCK";
                    snprintf(label, sizeof(label), "[%s] %s (%d dBm)", auth, done->aps[i].ssid, done->aps[i].rssi);
                    lv_obj_t *btn_ap = lv_list_add_btn(r->list, NULL, label);

                    wifi_ap_ud_t *ud = (wifi_ap_ud_t *)lv_mem_alloc(sizeof(wifi_ap_ud_t));
                    if (!ud) continue;
                    ud->refs = r;
                    strncpy(ud->ssid, done->aps[i].ssid, sizeof(ud->ssid));
                    ud->ssid[sizeof(ud->ssid) - 1] = '\0';

                    lv_obj_add_event_cb(
                        btn_ap,
                        [](lv_event_t *e) {
                            ui_click();
                            wifi_ap_ud_t *ud = (wifi_ap_ud_t *)lv_event_get_user_data(e);
                            if (!ud || !ud->refs) return;
                            wifi_ui_refs_t *r = ud->refs;
                            if (!r->ta_ssid || !r->ta_pass || !lv_obj_is_valid(r->ta_ssid) || !lv_obj_is_valid(r->ta_pass)) return;
                            lv_textarea_set_text(r->ta_ssid, ud->ssid);
                            lv_event_send(r->ta_pass, LV_EVENT_FOCUSED, NULL);
                        },
                        LV_EVENT_CLICKED, ud);
                    lv_obj_add_event_cb(
                        btn_ap,
                        [](lv_event_t *e) {
                            wifi_ap_ud_t *ud = (wifi_ap_ud_t *)lv_event_get_user_data(e);
                            if (ud) lv_mem_free(ud);
                        },
                        LV_EVENT_DELETE, ud);
                }

                lv_mem_free(done);
                s_wifi_ui_scan_running = false;
            },
            done);

        if (req) lv_mem_free(req);
        vTaskDelete(NULL);
    };

    xTaskCreate(worker, "wifi_scan", 4096, req, 2, NULL);
}