#include "ui_mp3.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_pins.h"
#include "ui_app_carousel.h"

#include "services/audio_es8311.h"
#include "services/sdcard_service.h"

#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3/minimp3.h"

static const char *TAG = "ui_mp3";

static lv_obj_t *s_mp3_screen = nullptr;
static lv_obj_t *s_list = nullptr;
static lv_obj_t *s_status = nullptr;
static lv_obj_t *s_btn_stop = nullptr;

static TaskHandle_t s_play_task = nullptr;
static volatile bool s_stop_req = false;
static volatile bool s_is_playing = false;

typedef struct {
    char *path;
    char *name;
} mp3_play_req_t;

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

static void set_status_text(const char *text)
{
    if (!s_status || !lv_obj_is_valid(s_status)) return;
    lv_label_set_text(s_status, text);
}

static void set_stop_enabled(bool en)
{
    if (!s_btn_stop || !lv_obj_is_valid(s_btn_stop)) return;
    if (en) {
        lv_obj_clear_state(s_btn_stop, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_btn_stop, LV_OPA_100, 0);
    } else {
        lv_obj_add_state(s_btn_stop, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_btn_stop, LV_OPA_40, 0);
    }
}

static void request_stop_playback(void)
{
    s_stop_req = true;
}

static void playback_task(void *arg)
{
    mp3_play_req_t *req = (mp3_play_req_t *)arg;
    if (!req || !req->path) {
        vTaskDelete(NULL);
        return;
    }

    s_play_task = xTaskGetCurrentTaskHandle();
    s_stop_req = false;
    s_is_playing = true;

    lv_async_call(
        [](void *p) {
            mp3_play_req_t *req = (mp3_play_req_t *)p;
            if (!s_mp3_screen || !lv_obj_is_valid(s_mp3_screen)) return;
            if (!s_status || !lv_obj_is_valid(s_status)) return;
            if (req && req->name) {
                lv_label_set_text_fmt(s_status, "Playing: %s", req->name);
            } else {
                set_status_text("Playing...");
            }
            set_stop_enabled(true);
        },
        req);

    FILE *f = fopen(req->path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open: %s", req->path);
        lv_async_call(
            [](void *) {
                set_status_text("Open failed");
                set_stop_enabled(false);
            },
            NULL);
        s_is_playing = false;
        s_play_task = nullptr;
        if (req->name) lv_mem_free(req->name);
        lv_mem_free(req->path);
        lv_mem_free(req);
        vTaskDelete(NULL);
        return;
    }

    mp3dec_t dec;
    mp3dec_init(&dec);

    constexpr int kInBufSize = 16 * 1024;
    // Keep large buffers off INTERNAL heap when PSRAM is available.
    uint8_t *inbuf = (uint8_t *)heap_caps_malloc(kInBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!inbuf) {
        inbuf = (uint8_t *)malloc(kInBufSize);
    }
    if (!inbuf) {
        fclose(f);
        lv_async_call(
            [](void *) {
                set_status_text("No memory");
                set_stop_enabled(false);
            },
            NULL);
        s_is_playing = false;
        s_play_task = nullptr;
        if (req->name) lv_mem_free(req->name);
        lv_mem_free(req->path);
        lv_mem_free(req);
        vTaskDelete(NULL);
        return;
    }

    int in_len = 0;
    bool eof = false;
    int current_rate = 0;

    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t mono_to_stereo[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (!s_stop_req) {
        if (!eof && in_len < (kInBufSize - 4096)) {
            const size_t n = fread(inbuf + in_len, 1, (size_t)(kInBufSize - in_len), f);
            if (n == 0) {
                eof = true;
            } else {
                in_len += (int)n;
            }
        }

        if (in_len <= 0) {
            if (eof) break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        mp3dec_frame_info_t info;
        const int samples = mp3dec_decode_frame(&dec, inbuf, in_len, pcm, &info);

        if (info.frame_bytes > 0 && info.frame_bytes <= in_len) {
            memmove(inbuf, inbuf + info.frame_bytes, (size_t)(in_len - info.frame_bytes));
            in_len -= info.frame_bytes;
        } else if (info.frame_bytes == 0) {
            // Need more data.
            if (eof) break;
            continue;
        } else {
            // Corrupt; resync by dropping a byte.
            memmove(inbuf, inbuf + 1, (size_t)(in_len - 1));
            in_len -= 1;
            continue;
        }

        if (samples <= 0 || info.hz <= 0) {
            continue;
        }

        if (current_rate != info.hz) {
            current_rate = info.hz;
            esp_err_t err = audio_es8311_stream_begin(current_rate);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "stream_begin(%d) failed: %s", current_rate, esp_err_to_name(err));
                lv_async_call(
                    [](void *) {
                        set_status_text("Audio init failed");
                        set_stop_enabled(false);
                    },
                    NULL);
                break;
            }
        }

        const int channels = (info.channels == 1) ? 1 : 2;
        const int total_samples = samples * channels;
        int out_samples = (channels == 1) ? (samples * 2) : total_samples;

        const int16_t *out = (const int16_t *)pcm;
        if (channels == 1) {
            const int16_t *in = (const int16_t *)pcm;
            const int max_i = (samples < (int)(sizeof(mono_to_stereo) / sizeof(mono_to_stereo[0]) / 2))
                                  ? samples
                                  : (int)(sizeof(mono_to_stereo) / sizeof(mono_to_stereo[0]) / 2);
            for (int i = 0; i < max_i; i++) {
                mono_to_stereo[2 * i + 0] = in[i];
                mono_to_stereo[2 * i + 1] = in[i];
            }
            out = mono_to_stereo;
            out_samples = max_i * 2;
        }

        const size_t bytes = (size_t)out_samples * sizeof(int16_t);
        (void)audio_es8311_stream_write(out, bytes, 2000);
    }

    fclose(f);
    free(inbuf);
    audio_es8311_stream_end();

    lv_async_call(
        [](void *p) {
            mp3_play_req_t *req = (mp3_play_req_t *)p;
            if (s_mp3_screen && lv_obj_is_valid(s_mp3_screen)) {
                if (s_stop_req) {
                    set_status_text("Stopped");
                } else {
                    set_status_text("Done");
                }
                set_stop_enabled(false);
            }
            if (req) {
                if (req->name) lv_mem_free(req->name);
                if (req->path) lv_mem_free(req->path);
                lv_mem_free(req);
            }
        },
        req);

    s_is_playing = false;
    s_play_task = nullptr;
    vTaskDelete(NULL);
}

static void populate_list(void)
{
    if (!s_list || !lv_obj_is_valid(s_list)) return;

    DIR *dir = opendir("/sdcard");
    if (!dir) {
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, "No /sdcard directory");
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        return;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!has_ext(ent->d_name, ".mp3")) continue;

        char vfs_path[300];
        snprintf(vfs_path, sizeof(vfs_path), "/sdcard/%s", ent->d_name);

        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, ent->d_name);
        char *path = (char *)lv_mem_alloc(strlen(vfs_path) + 1);
        if (!path) continue;
        strcpy(path, vfs_path);

        lv_obj_add_event_cb(
            btn,
            [](lv_event_t *e) {
                ui_click();
                const char *path = (const char *)lv_event_get_user_data(e);
                if (!path) return;

                if (s_is_playing) {
                    set_status_text("Busy (stop first)");
                    return;
                }

                mp3_play_req_t *req = (mp3_play_req_t *)lv_mem_alloc(sizeof(mp3_play_req_t));
                if (!req) {
                    set_status_text("No memory");
                    return;
                }
                memset(req, 0, sizeof(*req));
                req->path = (char *)lv_mem_alloc(strlen(path) + 1);
                if (req->path) strcpy(req->path, path);

                // Show just the filename in the status.
                const char *name = strrchr(path, '/');
                name = name ? (name + 1) : path;
                req->name = (char *)lv_mem_alloc(strlen(name) + 1);
                if (req->name) strcpy(req->name, name);

                if (!req->path || !req->name) {
                    if (req->name) lv_mem_free(req->name);
                    if (req->path) lv_mem_free(req->path);
                    lv_mem_free(req);
                    set_status_text("No memory");
                    return;
                }

                s_stop_req = false;
                ESP_LOGI(TAG, "Play: %s", req->path);

                auto task_fn = [](void *p) { playback_task(p); };

#if CONFIG_FREERTOS_UNICORE
                xTaskCreate(task_fn, "mp3_play", 8192, req, 3, NULL);
#else
                // Keep decode off the LVGL core.
                xTaskCreatePinnedToCore(task_fn, "mp3_play", 8192, req, 3, NULL, 0);
#endif
            },
            LV_EVENT_CLICKED,
            path);

        // If the list is deleted, free the per-item userdata.
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t *e) {
                char *path = (char *)lv_event_get_user_data(e);
                if (path) lv_mem_free(path);
            },
            LV_EVENT_DELETE,
            path);

        if (++count >= 40) break;
    }
    closedir(dir);

    if (count == 0) {
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, "No .mp3 found in /sdcard");
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
}

esp_err_t ui_mp3_open(void)
{
    if (s_mp3_screen && lv_obj_is_valid(s_mp3_screen)) {
        lv_scr_load_anim(s_mp3_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        return ESP_OK;
    }

    s_mp3_screen = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(s_mp3_screen, 8, 0);
    lv_obj_set_style_bg_color(s_mp3_screen, lv_color_hex(0x000000), 0);

    lv_obj_t *hdr = lv_obj_create(s_mp3_screen);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, 44);
    lv_obj_set_style_pad_all(hdr, 6, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_exit = lv_btn_create(hdr);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(
        btn_exit,
        [](lv_event_t *) {
            ui_click();
            request_stop_playback();
            lv_obj_t *carousel = ui_app_carousel_get_screen();
            if (carousel && lv_obj_is_valid(carousel)) {
                ui_load_screen_deferred(carousel, true);
            } else {
                ui_app_carousel_init();
            }
        },
        LV_EVENT_CLICKED,
        NULL);

    s_btn_stop = lv_btn_create(hdr);
    lv_obj_set_size(s_btn_stop, 60, 32);
    lv_obj_align(s_btn_stop, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *lbl_stop = lv_label_create(s_btn_stop);
    lv_label_set_text(lbl_stop, "Stop");
    lv_obj_center(lbl_stop);
    lv_obj_add_event_cb(
        s_btn_stop,
        [](lv_event_t *) {
            ui_click();
            request_stop_playback();
            set_status_text("Stopping...");
        },
        LV_EVENT_CLICKED,
        NULL);

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "MP3");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *cont = lv_obj_create(s_mp3_screen);
    set_content_area(cont);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    s_status = lv_label_create(cont);
    lv_obj_set_width(s_status, lv_pct(100));
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xcccccc), 0);
    lv_label_set_text(s_status, "Select a file");
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 0, 0);

    s_list = lv_list_create(cont);
    lv_obj_set_size(s_list, lv_pct(100), APP_LCD_V_RES - 52 - 8 - 22);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x0a0a0a), 0);

    set_stop_enabled(false);

    lv_obj_add_event_cb(
        s_mp3_screen,
        [](lv_event_t *) {
            request_stop_playback();
            s_mp3_screen = nullptr;
            s_list = nullptr;
            s_status = nullptr;
            s_btn_stop = nullptr;
        },
        LV_EVENT_DELETE,
        NULL);

    lv_scr_load_anim(s_mp3_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    if (!sdcard_service_is_mounted()) {
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, "Mounting SD...");
        lv_obj_add_state(btn, LV_STATE_DISABLED);

        typedef struct {
            lv_obj_t *btn;
            uint32_t start_ms;
        } mount_btn_t;

        mount_btn_t *mctx = (mount_btn_t *)lv_mem_alloc(sizeof(mount_btn_t));
        if (mctx) {
            mctx->btn = btn;
            mctx->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            lv_timer_create(
                [](lv_timer_t *t) {
                    mount_btn_t *ctx = (mount_btn_t *)t->user_data;
                    if (!ctx || !ctx->btn || !lv_obj_is_valid(ctx->btn)) {
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
            (void)arg;
            sdcard_service_mount();
            lv_async_call(
                [](void *) {
                    if (!s_list || !lv_obj_is_valid(s_list)) return;
                    lv_obj_clean(s_list);
                    if (!sdcard_service_is_mounted()) {
                        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, "Mount failed");
                        lv_obj_add_state(btn, LV_STATE_DISABLED);
                        if (s_status && lv_obj_is_valid(s_status)) {
                            lv_label_set_text_fmt(s_status, "Mount failed: %s", esp_err_to_name(sdcard_service_last_error()));
                        }
                        return;
                    }
                    set_status_text("Select a file");
                    populate_list();
                },
                NULL);
            vTaskDelete(NULL);
        };

#if CONFIG_FREERTOS_UNICORE
        xTaskCreate(mount_task, "mp3_mount", 4096, NULL, 2, NULL);
#else
        xTaskCreatePinnedToCore(mount_task, "mp3_mount", 4096, NULL, 2, NULL, 0);
#endif
        return ESP_OK;
    }

    populate_list();
    return ESP_OK;
}
