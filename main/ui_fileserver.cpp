#include "ui_fileserver.h"

#include <stdio.h>

#include "lvgl.h"

#include "app_pins.h"
#include "ui_app_carousel.h"

#include "services/audio_es8311.h"
#include "services/fileserver_service.h"
#include "services/sdcard_service.h"

static lv_obj_t *s_screen = nullptr;
static lv_obj_t *s_lbl_status = nullptr;

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
    ui_screen_load_req_t *req = (ui_screen_load_req_t *)lv_mem_alloc(sizeof(ui_screen_load_req_t));
    if (!req) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, auto_del);
        return;
    }
    req->scr = scr;
    req->auto_del = auto_del;
    lv_async_call(ui_screen_load_async_cb, req);
}

static inline void ui_click(void)
{
    audio_es8311_play_click();
}

static void set_content_area(lv_obj_t *cont)
{
    constexpr int kTop = 52;
    lv_obj_set_size(cont, APP_LCD_H_RES, APP_LCD_V_RES - kTop);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, kTop);
}

static lv_obj_t *make_screen(const char *title)
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

    lv_obj_t *btn_exit = lv_btn_create(hdr);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(btn_exit, [](lv_event_t *) {
        ui_click();
        fileserver_service_stop();
        lv_obj_t *carousel = ui_app_carousel_get_screen();
        if (carousel && lv_obj_is_valid(carousel)) {
            ui_load_screen_deferred(carousel, true);
        } else {
            ui_app_carousel_init();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

static void on_screen_delete(lv_event_t *)
{
    // If user navigates away by any means, ensure the server is stopped.
    fileserver_service_stop();
    s_screen = nullptr;
    s_lbl_status = nullptr;
}

esp_err_t ui_fileserver_open(void)
{
    // Build UI first, then start the service (so we can show errors cleanly).
    s_screen = make_screen("FileServer");
    lv_obj_add_event_cb(s_screen, on_screen_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *cont = lv_obj_create(s_screen);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Starting SoftAP + Web UI...\n");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_status = lv_label_create(cont);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_white(), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_LEFT, 0, 40);

    // Best-effort SD mount so /sdcard is available from the web UI.
    if (!sdcard_service_is_mounted()) {
        (void)sdcard_service_mount();
    }

    const esp_err_t err = fileserver_service_start();
    if (err == ESP_OK) {
        lv_label_set_text_fmt(s_lbl_status,
                              "AP SSID: %s\nIP: http://%s/\nRoots: flash=/storage, sd=/sdcard\n",
                              fileserver_service_ap_ssid(), fileserver_service_ap_ip());
    } else {
        lv_label_set_text_fmt(s_lbl_status, "Start failed: %d", (int)err);
    }

    // Load screen and delete previous active screen to avoid leaks.
    lv_scr_load_anim(s_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    return err;
}
