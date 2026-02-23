#include "ui_radio.h"
#include "lvgl.h"
#include "radio_player.h"
#include "esp_log.h"

static const char *TAG = "ui_radio";
static lv_obj_t *s_radio_screen = nullptr;

static const char *kStations[] = {
    "http://icecast.omroep.nl/radio1-bb-mp3",
    "http://icecast.omroep.nl/radio2-bb-mp3",
    "http://icecast.omroep.nl/3fm-bb-mp3",
    "http://icecast.omroep.nl/radio4-bb-mp3",
    "http://icecast.omroep.nl/radio5-bb-mp3"
};
static const char *kStationNames[] = {
    "Radio 1",
    "Radio 2",
    "3FM",
    "Radio 4",
    "Radio 5"
};
static const int kStationCount = sizeof(kStations) / sizeof(kStations[0]);

esp_err_t ui_radio_open(void)
{
    if (s_radio_screen && lv_obj_is_valid(s_radio_screen)) {
        lv_scr_load_anim(s_radio_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        return ESP_OK;
    }

    s_radio_screen = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(s_radio_screen, 8, 0);
    lv_obj_set_style_bg_color(s_radio_screen, lv_color_hex(0x000000), 0);

    lv_obj_t *hdr = lv_obj_create(s_radio_screen);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, 44);
    lv_obj_set_style_pad_all(hdr, 6, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "Internet Radio");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *list = lv_list_create(s_radio_screen);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0a0a0a), 0);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

    for (int i = 0; i < kStationCount; ++i) {
        lv_obj_t *btn = lv_list_add_btn(list, NULL, kStationNames[i]);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            int idx = (int)(uintptr_t)lv_event_get_user_data(e);
            ESP_LOGI(TAG, "Selected station %d: %s", idx, kStations[idx]);
            radio_player_play(kStations[idx]);
        }, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_obj_add_event_cb(s_radio_screen, [](lv_event_t *) {
        s_radio_screen = nullptr;
    }, LV_EVENT_DELETE, NULL);

    lv_scr_load_anim(s_radio_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    return ESP_OK;
}
