#include "home.h"

#include <stdio.h>
#include <string.h>

#include "app/clock.h"
#include "app/fonts.h"
#include "ble.h"
#include "ble_screen.h"
#include "clock_screen.h"
#include "gallery.h"
#include "library.h"
#include "music_screen.h"
#include "settings_screen.h"
#include "storage/storage.h"
#include "theme.h"
#include "video.h"
#include "widgets.h"
#include "wifi.h"
#include "wifi_screen.h"

typedef struct {
    const char *glyph;
    const char *name;
    void (*open)(void);
} app_tile_t;

static const app_tile_t TILES[] = {
    {"书", "阅读", ui_library_show},
    {"图", "图片", ui_gallery_show},
    {"音", "音乐", ui_music_show},
    {"影", "视频", ui_video_show},
    {"蓝", "蓝牙", ui_ble_show},
    {"时", "时钟", ui_clock_show},
    {"设", "设置", ui_settings_show},
};

#define TILE_COUNT ((int)(sizeof(TILES) / sizeof(TILES[0])))

static lv_obj_t *s_screen;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_net;
static lv_obj_t *s_storage;
static lv_timer_t *s_tick;

static void tile_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= TILE_COUNT) {
        return;
    }
    TILES[index].open();
}

static void update_status(void)
{
    if (!s_screen || lv_screen_active() != s_screen) {
        return;
    }

    char buf[64];
    clock_format_time(buf, sizeof(buf), false);
    lv_label_set_text(s_time, buf);

    clock_format_date(buf, sizeof(buf));
    lv_label_set_text(s_date, buf);

    char net[96];
    if (net_wifi_is_connected()) {
        char ssid[40] = {0};
        net_wifi_get_ssid(ssid, sizeof(ssid));
        snprintf(net, sizeof(net), "WiFi %s", ssid);
    } else if (net_wifi_has_credentials()) {
        snprintf(net, sizeof(net), "WiFi 连接中");
    } else {
        snprintf(net, sizeof(net), "WiFi 未连接");
    }
    if (ble_is_ready()) {
        size_t len = strlen(net);
        snprintf(net + len, sizeof(net) - len, " · BLE");
    }
    lv_label_set_text(s_net, net);

    uint64_t free_b = 0;
    uint64_t total_b = 0;
    storage_free_space(&free_b, &total_b);
    if (total_b > 0) {
        snprintf(buf, sizeof(buf), "SD 可用 %.1f GB / %.1f GB", free_b / 1073741824.0,
                 total_b / 1073741824.0);
    } else {
        snprintf(buf, sizeof(buf), "SD 卡不可用");
    }
    lv_label_set_text(s_storage, buf);
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    update_status();
}

static void make_tile(lv_obj_t *parent, int index)
{
    const ui_theme_t *t = ui_theme();
    const app_tile_t *app = &TILES[index];

    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_set_size(tile, 218, 132);
    lv_obj_set_style_bg_color(tile, t->panel, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 18, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 0, 0);

    lv_obj_t *glyph = ui_label(tile, app->glyph, fonts_get(1), t->accent);
    lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *name = ui_label(tile, app->name, fonts_ui(), t->fg);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
}

void ui_home_show(void)
{
    if (s_screen) {
        update_status();
        lv_screen_load(s_screen);
        return;
    }

    const ui_theme_t *t = ui_theme();
    s_screen = ui_screen_new();

    /* 状态栏 */
    lv_obj_t *bar = lv_obj_create(s_screen);
    lv_obj_set_size(bar, 448, 104);
    lv_obj_set_pos(bar, 16, 14);
    lv_obj_set_style_bg_color(bar, t->panel, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 18, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 14, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_time = ui_label(bar, "--:--", fonts_get(1), t->fg);
    lv_obj_set_pos(s_time, 4, 2);

    s_date = ui_label(bar, "", fonts_ui(), t->muted);
    lv_obj_set_pos(s_date, 4, 52);
    lv_obj_set_width(s_date, 250);
    lv_label_set_long_mode(s_date, LV_LABEL_LONG_MODE_DOTS);

    s_net = ui_label(bar, "", fonts_ui(), t->accent);
    lv_obj_set_width(s_net, 170);
    lv_obj_set_pos(s_net, 258, 8);
    lv_label_set_long_mode(s_net, LV_LABEL_LONG_MODE_DOTS);

    s_storage = ui_label(bar, "", fonts_ui(), t->muted);
    lv_obj_set_width(s_storage, 170);
    lv_obj_set_pos(s_storage, 258, 56);
    lv_label_set_long_mode(s_storage, LV_LABEL_LONG_MODE_DOTS);

    /* 应用宫格 */
    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_set_size(grid, 448, 548);
    lv_obj_set_pos(grid, 16, 132);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < TILE_COUNT; i++) {
        make_tile(grid, i);
    }

    lv_obj_t *footer = ui_label(s_screen, "SD:/books · /pictures · /music · /videos", fonts_ui(), t->muted);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -16);

    if (!s_tick) {
        s_tick = lv_timer_create(tick_cb, 1000, NULL);
    }

    lv_screen_load(s_screen);
    update_status();
}

void ui_home_refresh(void)
{
    update_status();
}

void ui_home_reset(void)
{
    /* 活动屏幕不能直接删，否则 LVGL 的屏幕指针会悬空 */
    if (!s_screen || lv_screen_active() == s_screen) {
        return;
    }
    lv_obj_delete(s_screen);
    s_screen = NULL;
    s_time = NULL;
    s_date = NULL;
    s_net = NULL;
    s_storage = NULL;
}
