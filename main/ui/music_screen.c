#include "music_screen.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "audio/player.h"
#include "ble.h"
#include "home.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"

#define MAX_TRACKS 64

static const char *const AUDIO_EXTS[] = {".mp3", ".wav"};

static lv_obj_t *s_screen;
static lv_obj_t *s_now;
static lv_obj_t *s_state_label;
static lv_obj_t *s_play_label;
static lv_obj_t *s_list;
static lv_timer_t *s_poll;

static media_file_t s_files[MAX_TRACKS];
static int s_count;
static int s_index = -1;
static volatile int s_state = AUDIO_EVT_IDLE;
static int s_last_state = -1;

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    ui_home_show();
}

static void on_state(int state)
{
    s_state = state;
}

static void play_index(int index)
{
    if (s_count <= 0) {
        return;
    }
    if (index < 0) {
        index = s_count - 1;
    } else if (index >= s_count) {
        index = 0;
    }
    s_index = index;
    if (audio_play_file(s_files[index].path) != ESP_OK) {
        ui_toast("无法播放：%s", s_files[index].name);
    }
}

static void play_pause_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) {
        return;
    }
    if (s_index < 0) {
        play_index(0);
        return;
    }
    if (audio_is_playing()) {
        audio_pause();
    } else {
        audio_resume();
    }
}

static void prev_cb(lv_event_t *e)
{
    (void)e;
    if (s_count > 0) {
        play_index(s_index - 1);
    }
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    if (s_count > 0) {
        play_index(s_index + 1);
    }
}

static void stop_cb(lv_event_t *e)
{
    (void)e;
    audio_stop();
    s_index = -1;
}

static void item_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index >= 0 && index < s_count) {
        play_index(index);
    }
}

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    audio_set_volume((int)lv_slider_get_value(slider));
}

static void update_player_ui(void)
{
    if (!s_screen || lv_screen_active() != s_screen) {
        return;
    }

    if (s_index >= 0 && s_index < s_count) {
        lv_label_set_text(s_now, s_files[s_index].name);
    } else {
        lv_label_set_text(s_now, "未选择曲目");
    }

    const char *text = "已停止";
    if (s_state == AUDIO_EVT_PLAYING) {
        text = "播放中";
    } else if (s_state == AUDIO_EVT_PAUSE) {
        text = "已暂停";
    } else if (s_state == AUDIO_EVT_NEXT) {
        text = "播放中";
    }
    lv_label_set_text(s_state_label, text);

    if (s_play_label) {
        lv_label_set_text(s_play_label, (s_state == AUDIO_EVT_PLAYING) ? "暂停" : "播放");
    }
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_state != s_last_state) {
        s_last_state = s_state;
        update_player_ui();
    }
}

static void rebuild_list(void)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);
    const ui_theme_t *t = ui_theme();

    if (s_count == 0) {
        lv_obj_t *hint = ui_label(s_list,
                                  "没有音乐\n\n把 .mp3 / .wav 放到 SD 卡\n/music 目录",
                                  fonts_ui(), t->muted);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(hint, 6, 0);
        lv_obj_set_style_pad_top(hint, 36, 0);
        return;
    }

    for (int i = 0; i < s_count; i++) {
        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_size(btn, LV_PCT(100), 92);
        lv_obj_set_style_bg_color(btn, (i == s_index) ? t->accent : t->panel, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_color_t fg = (i == s_index) ? lv_color_hex(0xFFFFFF) : t->fg;
        lv_obj_t *name = ui_label(btn, s_files[i].name, fonts_ui(), fg);
        lv_obj_set_pos(name, 18, 2);
        lv_obj_set_width(name, 380);
        lv_obj_set_height(name, 43);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        char meta[48];
        snprintf(meta, sizeof(meta), "%.1f MB", s_files[i].size / 1048576.0);
        lv_obj_t *sub = ui_label(btn, meta, fonts_ui(),
                                 (i == s_index) ? lv_color_hex(0xEAF2FF) : t->muted);
        lv_obj_set_pos(sub, 18, 46);

        lv_obj_add_event_cb(btn, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    if (!storage_ready()) {
        storage_init();
    }
    s_count = storage_scan_media("music", AUDIO_EXTS, 2, s_files, MAX_TRACKS);
    rebuild_list();
    ui_toast("已刷新");
}

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    /* 离开音乐页释放 I2S/codec 占用的 DMA 内存，供 BLE/视频使用 */
    audio_shutdown();
    s_screen = NULL;
    s_list = NULL;
    s_now = NULL;
    s_state_label = NULL;
    s_play_label = NULL;
    if (s_poll) {
        lv_timer_delete(s_poll);
        s_poll = NULL;
    }
}

void ui_music_show(void)
{
    /* 音频与 BLE 争抢 DMA 内存，进音乐页前先停掉 BLE 主机 */
    if (ble_is_ready()) {
        ble_stop();
    }
    s_state = AUDIO_EVT_IDLE;
    s_last_state = -1;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }

    const ui_theme_t *t = ui_theme();
    s_screen = ui_screen_new();
    lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *back = ui_button(s_screen, "首页", 84, 46, back_home_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "音乐", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    lv_obj_t *refresh = ui_button(s_screen, "刷新", 84, 46, refresh_cb, NULL);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -20, 20);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, 448, 468);
    lv_obj_set_pos(s_list, 16, 92);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_size(panel, 448, 212);
    lv_obj_set_pos(panel, 16, 572);
    lv_obj_set_style_bg_color(panel, t->panel, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_now = ui_label(panel, "未选择曲目", fonts_ui(), t->fg);
    lv_obj_set_pos(s_now, 4, 2);
    lv_obj_set_width(s_now, 410);
    lv_label_set_long_mode(s_now, LV_LABEL_LONG_MODE_DOTS);

    s_state_label = ui_label(panel, "已停止", fonts_ui(), t->muted);
    lv_obj_set_pos(s_state_label, 4, 34);

    lv_obj_t *prev = ui_button(panel, "上一首", 92, 52, prev_cb, NULL);
    lv_obj_set_pos(prev, 4, 68);
    lv_obj_t *pp = ui_button(panel, "播放", 92, 52, play_pause_cb, NULL);
    lv_obj_set_style_bg_color(pp, t->accent, 0);
    s_play_label = lv_obj_get_child(pp, 0);
    if (s_play_label) {
        lv_obj_set_style_text_color(s_play_label, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_set_pos(pp, 104, 68);
    lv_obj_t *next = ui_button(panel, "下一首", 92, 52, next_cb, NULL);
    lv_obj_set_pos(next, 204, 68);
    lv_obj_t *stop = ui_button(panel, "停止", 92, 52, stop_cb, NULL);
    lv_obj_set_pos(stop, 304, 68);

    lv_obj_t *volume_hint = ui_label(panel, "音量", fonts_ui(), t->muted);
    lv_obj_set_pos(volume_hint, 4, 132);

    lv_obj_t *slider = lv_slider_create(panel);
    lv_obj_set_size(slider, 280, 10);
    lv_obj_set_pos(slider, 62, 142);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, audio_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_count = storage_scan_media("music", AUDIO_EXTS, 2, s_files, MAX_TRACKS);
    rebuild_list();

    if (!audio_ready()) {
        if (audio_init() != ESP_OK) {
            ui_toast("音频初始化失败（ES8311 不可用）");
        }
    }
    audio_set_state_cb(on_state);

    if (!s_poll) {
        s_poll = lv_timer_create(poll_cb, 300, NULL);
    }
    s_last_state = -1;
    update_player_ui();
    lv_screen_load(s_screen);
}
