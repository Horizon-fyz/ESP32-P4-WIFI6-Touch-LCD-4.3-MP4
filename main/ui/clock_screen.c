#include "clock_screen.h"

#include <stdio.h>

#include "app/clock.h"
#include "app/fonts.h"
#include "home.h"
#include "theme.h"
#include "widgets.h"
#include "wifi.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_sync;
static lv_timer_t *s_tick;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_home_show();
}

static void sync_cb(lv_event_t *e)
{
    (void)e;
    if (net_wifi_is_connected()) {
        net_wifi_sync_time();
        ui_toast("正在网络校时…");
    } else {
        ui_toast("请先在「设置 - WiFi」连接网络");
    }
}

static void update(void)
{
    if (!s_screen) {
        return;
    }
    char buf[96];
    clock_format_time(buf, sizeof(buf), true);
    lv_label_set_text(s_time, buf);

    clock_format_date(buf, sizeof(buf));
    lv_label_set_text(s_date, buf);

    if (clock_is_synced()) {
        lv_label_set_text(s_sync, "已通过 SNTP 校准（时区 CST-8）");
    } else {
        lv_label_set_text(s_sync, "尚未校时：连接 WiFi 后点击下方按钮");
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    update();
}

void ui_clock_show(void)
{
    const ui_theme_t *t = ui_theme();
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_screen = ui_screen_new();

    lv_obj_t *back = ui_button(s_screen, "返回", 84, 46, back_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "时钟", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 448, 300);
    lv_obj_set_pos(card, 16, 110);
    lv_obj_set_style_bg_color(card, t->panel, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_time = ui_label(card, "--:--:--", fonts_get(2), t->fg);
    s_date = ui_label(card, "未校时", fonts_get(1), t->muted);
    s_sync = ui_label(card, "连接 WiFi 后自动校时", fonts_ui(), t->accent);
    lv_obj_set_width(s_sync, LV_PCT(100));
    lv_obj_set_style_text_align(s_sync, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_sync, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *sync = ui_button(s_screen, "立即校时", 200, 64, sync_cb, NULL);
    lv_obj_set_style_bg_color(sync, t->accent, 0);
    lv_obj_t *sync_label = lv_obj_get_child(sync, 0);
    if (sync_label) {
        lv_obj_set_style_text_color(sync_label, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_align(sync, LV_ALIGN_TOP_MID, 0, 440);

    lv_obj_t *hint = ui_label(s_screen,
                              "时间通过 NTP（ntp.aliyun.com）获取，\n"
                              "断网后由内部时钟继续走时。",
                              fonts_ui(), t->muted);
    lv_obj_set_width(hint, 420);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(hint, 6, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 530);

    if (!s_tick) {
        s_tick = lv_timer_create(tick_cb, 500, NULL);
    }
    lv_screen_load(s_screen);
    update();
}
