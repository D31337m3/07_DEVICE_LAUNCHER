#include "ui_media.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_pins.h"
#include "ui_app_carousel.h"

#include "services/audio_es8311.h"
#include "services/sdcard_service.h"

#if LV_USE_GIF
#include "extra/libs/gif/lv_gif.h"
#endif

static const char *TAG = "ui_media";

static lv_obj_t *s_media_screen = nullptr;

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

static bool has_ext(const char *name, const char *ext)
{
    const size_t n = strlen(name);
    const size_t e = strlen(ext);
    if (n < e + 1) return false;
    const char *p = name + (n - e);
    for (size_t i = 0; i < e; i++) {
        char a = p[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static lv_obj_t *make_screen(const char *title, bool show_back)
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

    if (show_back) {
        lv_obj_t *btn_back = lv_btn_create(hdr);
        lv_obj_set_size(btn_back, 60, 32);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *lbl = lv_label_create(btn_back);
        lv_label_set_text(lbl, "Back");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn_back, [](lv_event_t *) {
            ui_click();
            if (s_media_screen && lv_obj_is_valid(s_media_screen)) {
                // Defer: don't delete the active screen inside its callback.
                ui_load_screen_deferred(s_media_screen, true);
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *btn_exit = lv_btn_create(hdr);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(btn_exit, [](lv_event_t *) {
        ui_click();
        lv_obj_t *carousel = ui_app_carousel_get_screen();
        if (carousel && lv_obj_is_valid(carousel)) {
            // Defer: don't delete the active screen inside its callback.
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

static void open_jpg_viewer(const char *vfs_path)
{
    const char *rel = vfs_path;
    if (strncmp(vfs_path, "/sdcard/", 8) == 0) rel = vfs_path + 8;
    char lv_path[300];
    snprintf(lv_path, sizeof(lv_path), "S:/%s", rel);

    lv_obj_t *scr = make_screen("JPG Viewer", true);
    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t *img = lv_img_create(cont);
    lv_img_set_src(img, lv_path);
    lv_obj_center(img);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

static void open_gif_player(const char *vfs_path)
{
#if !LV_USE_GIF
    lv_obj_t *scr = make_screen("GIF Player", true);
    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "GIF support is disabled in config");
    lv_obj_center(lbl);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    return;
#else
    FILE *f = fopen(vfs_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open GIF: %s", vfs_path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (2 * 1024 * 1024)) {
        fclose(f);
        ESP_LOGW(TAG, "GIF size invalid: %ld", sz);
        return;
    }

    uint8_t *buf = (uint8_t *)lv_mem_alloc((size_t)sz);
    if (!buf) {
        fclose(f);
        ESP_LOGW(TAG, "Out of memory reading GIF");
        return;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        lv_mem_free(buf);
        ESP_LOGW(TAG, "Short read reading GIF");
        return;
    }
    fclose(f);

    lv_obj_t *scr = make_screen("GIF Player", true);
    lv_obj_t *cont = lv_obj_create(scr);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t *gif = lv_gif_create(cont);
    lv_obj_center(gif);

    lv_img_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    dsc.data = buf;
    dsc.data_size = (uint32_t)sz;
    lv_gif_set_src(gif, &dsc);

    lv_obj_add_event_cb(scr, [](lv_event_t *e) {
        uint8_t *buf = (uint8_t *)lv_event_get_user_data(e);
        if (buf) lv_mem_free(buf);
    }, LV_EVENT_DELETE, buf);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
#endif
}

static void populate_list(lv_obj_t *list)
{
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        lv_obj_t *btn = lv_list_add_btn(list, NULL, "No /sdcard directory");
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        return;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        const bool is_jpg = has_ext(ent->d_name, ".jpg") || has_ext(ent->d_name, ".jpeg");
        const bool is_gif = has_ext(ent->d_name, ".gif");
        if (!is_jpg && !is_gif) continue;

        char vfs_path[300];
        snprintf(vfs_path, sizeof(vfs_path), "/sdcard/%s", ent->d_name);

        lv_obj_t *btn = lv_list_add_btn(list, NULL, ent->d_name);
        char *p = (char *)lv_mem_alloc(strlen(vfs_path) + 1);
        if (!p) continue;
        strcpy(p, vfs_path);
        lv_obj_add_event_cb(btn,
                            [](lv_event_t *e) {
                                ui_click();
                                char *path = (char *)lv_event_get_user_data(e);
                                if (!path) return;
                                if (has_ext(path, ".gif")) open_gif_player(path);
                                else open_jpg_viewer(path);
                            },
                            LV_EVENT_CLICKED, p);
        lv_obj_add_event_cb(btn,
                            [](lv_event_t *e) {
                                char *path = (char *)lv_event_get_user_data(e);
                                if (path) lv_mem_free(path);
                            },
                            LV_EVENT_DELETE, p);

        if (++count >= 40) break;
    }
    closedir(dir);

    if (count == 0) {
        lv_obj_t *btn = lv_list_add_btn(list, NULL, "No .jpg/.gif found in /sdcard");
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
}

esp_err_t ui_media_open(void)
{
    if (s_media_screen && lv_obj_is_valid(s_media_screen)) {
        lv_scr_load_anim(s_media_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        return ESP_OK;
    }

    s_media_screen = make_screen("Media", false);
    lv_obj_t *cont = lv_obj_create(s_media_screen);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0a0a0a), 0);

    lv_obj_add_event_cb(s_media_screen, [](lv_event_t *) {
        s_media_screen = nullptr;
    }, LV_EVENT_DELETE, NULL);

    lv_scr_load_anim(s_media_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    if (!sdcard_service_is_mounted()) {
        lv_obj_t *btn = lv_list_add_btn(list, NULL, "Mounting SD...");
        lv_obj_add_state(btn, LV_STATE_DISABLED);

        typedef struct {
            lv_obj_t *list;
            lv_obj_t *btn;
            uint32_t start_ms;
        } mount_ui_t;

        mount_ui_t *mctx = (mount_ui_t *)lv_mem_alloc(sizeof(mount_ui_t));
        if (mctx) {
            mctx->list = list;
            mctx->btn = btn;
            mctx->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            lv_timer_create(
                [](lv_timer_t *t) {
                    mount_ui_t *ctx = (mount_ui_t *)t->user_data;
                    if (!ctx || !ctx->list || !ctx->btn || !lv_obj_is_valid(ctx->list) || !lv_obj_is_valid(ctx->btn)) {
                        if (ctx) lv_mem_free(ctx);
                        lv_timer_del(t);
                        return;
                    }
                    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                    uint32_t el = (now_ms - ctx->start_ms) / 1000U;
                    lv_obj_t *lbl = lv_obj_get_child(ctx->btn, 0);
                    if (lbl && lv_obj_is_valid(lbl)) {
                        lv_label_set_text_fmt(lbl, "Mounting SD... (%lus) %s", (unsigned long)el, sdcard_service_last_status());
                    }
                },
                250,
                mctx);
        }
        auto mount_task = [](void *arg) {
            lv_obj_t *list = (lv_obj_t *)arg;
            sdcard_service_mount();
            lv_async_call(
                [](void *p) {
                    lv_obj_t *list = (lv_obj_t *)p;
                    if (!list || !lv_obj_is_valid(list)) return;
                    lv_obj_clean(list);
                    if (!sdcard_service_is_mounted()) {
                        lv_obj_t *btn = lv_list_add_btn(list, NULL, "Mount failed");
                        lv_obj_add_state(btn, LV_STATE_DISABLED);
                        lv_list_add_text(list, esp_err_to_name(sdcard_service_last_error()));
                        return;
                    }
                    populate_list(list);
                },
                list);
            vTaskDelete(NULL);
        };

#if CONFIG_FREERTOS_UNICORE
        xTaskCreate(mount_task, "media_mount", 4096, list, 2, NULL);
#else
        // Prefer core 0 for background mount to keep UI smoother.
        xTaskCreatePinnedToCore(mount_task, "media_mount", 4096, list, 2, NULL, 0);
#endif
        return ESP_OK;
    }

    populate_list(list);
    return ESP_OK;
}
