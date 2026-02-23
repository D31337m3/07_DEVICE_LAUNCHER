#include "lvgl_fs_sdcard.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#if LV_USE_SJPG
#include "extra/libs/sjpg/lv_sjpg.h"
#endif

static const char *TAG = "lv_fs_sd";

static bool s_inited = false;

static void make_full_path(char *out, size_t out_len, const char *path)
{
    const char *p = path;

    // Some LVGL call sites may still pass a drive prefix.
    if (p && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        p += 2;
    }
    while (*p == '/' || *p == '\\') p++;
    snprintf(out, out_len, "/sdcard/%s", p);
}

static void *fs_open(lv_fs_drv_t *, const char *path, lv_fs_mode_t mode)
{
    char full[256];
    make_full_path(full, sizeof(full), path);

    const char *m = (mode == LV_FS_MODE_WR) ? "wb" : "rb";
    FILE *f = fopen(full, m);
    return (void *)f;
}

static lv_fs_res_t fs_close(lv_fs_drv_t *, void *file_p)
{
    FILE *f = (FILE *)file_p;
    if (!f) return LV_FS_RES_INV_PARAM;
    fclose(f);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    FILE *f = (FILE *)file_p;
    if (!f) return LV_FS_RES_INV_PARAM;
    size_t r = fread(buf, 1, btr, f);
    if (br) *br = (uint32_t)r;
    if (r == 0 && ferror(f)) return LV_FS_RES_UNKNOWN;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    FILE *f = (FILE *)file_p;
    if (!f) return LV_FS_RES_INV_PARAM;

    int w = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) w = SEEK_CUR;
    else if (whence == LV_FS_SEEK_END) w = SEEK_END;

    return fseek(f, (long)pos, w) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *, void *file_p, uint32_t *pos_p)
{
    FILE *f = (FILE *)file_p;
    if (!f || !pos_p) return LV_FS_RES_INV_PARAM;

    long p = ftell(f);
    if (p < 0) return LV_FS_RES_UNKNOWN;
    *pos_p = (uint32_t)p;
    return LV_FS_RES_OK;
}

esp_err_t lvgl_fs_sdcard_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'S';
    drv.open_cb = fs_open;
    drv.close_cb = fs_close;
    drv.read_cb = fs_read;
    drv.seek_cb = fs_seek;
    drv.tell_cb = fs_tell;
    lv_fs_drv_register(&drv);

#if LV_USE_SJPG
    // Register JPG decoder (uses lv_fs under the hood)
    lv_split_jpeg_init();
#endif

    s_inited = true;
    ESP_LOGI(TAG, "LVGL FS registered: S:/ -> /sdcard/");
    return ESP_OK;
}
