#include "wifi_screen.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"
#include "wifi.h"

#define MAX_APS 24

static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_modal;
static lv_obj_t *s_mask;
static lv_obj_t *s_ta;
static lv_timer_t *s_poll;
static char s_pending_ssid[33];

static net_wifi_ap_t s_aps[MAX_APS];
static int s_ap_count;
static volatile bool s_scan_running;
static volatile bool s_scan_done;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_home_show();
}

static void close_modal(void)
{
    if (s_modal) {
        lv_obj_delete(s_modal);
        s_modal = NULL;
    }
    if (s_mask) {
        lv_obj_delete(s_mask);
        s_mask = NULL;
    }
    s_ta = NULL;
}

static void modal_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

static void connect_with(const char *ssid, const char *pass)
{
    if (net_wifi_set_credentials(ssid, pass) == ESP_OK) {
        ui_toast("正在连接 %s…", ssid);
    } else {
        ui_toast("保存 WiFi 配置失败");
    }
}

static void modal_ok_cb(lv_event_t *e)
{
    (void)e;
    const char *pass = s_ta ? lv_textarea_get_text(s_ta) : "";
    char ssid[33];
    snprintf(ssid, sizeof(ssid), "%s", s_pending_ssid);
    close_modal();
    connect_with(ssid, pass);
}

static void password_modal(const char *ssid)
{
    const ui_theme_t *t = ui_theme();
    close_modal();

    s_mask = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mask, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_mask, 0, 0);
    lv_obj_set_style_radius(s_mask, 0, 0);
    lv_obj_remove_flag(s_mask, LV_OBJ_FLAG_SCROLLABLE);

    s_modal = lv_obj_create(s_mask);
    lv_obj_set_size(s_modal, 440, 300);
    lv_obj_align(s_modal, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(s_modal, t->panel, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_modal, 16, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* 用静态缓冲记录 SSID，供确认按钮读取 */
    snprintf(s_pending_ssid, sizeof(s_pending_ssid), "%s", ssid);

    char title[64];
    snprintf(title, sizeof(title), "连接 %s", ssid);
    lv_obj_t *label = ui_label(s_modal, title, fonts_ui(), t->fg);
    lv_obj_set_width(label, 400);
    lv_obj_set_pos(label, 20, 10);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *hint = ui_label(s_modal, "请输入 WiFi 密码", fonts_ui(), t->muted);
    lv_obj_set_pos(hint, 20, 52);

    lv_obj_t *ta = lv_textarea_create(s_modal);
    lv_obj_set_size(ta, 400, 58);
    lv_obj_set_pos(ta, 20, 90);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "密码");
    lv_obj_set_style_text_font(ta, fonts_ui(), 0);
    s_ta = ta;

    lv_obj_t *cancel = ui_button(s_modal, "取消", 120, 52, modal_cancel_cb, NULL);
    lv_obj_set_pos(cancel, 20, 164);

    lv_obj_t *ok = ui_button(s_modal, "连接", 120, 52, modal_ok_cb, NULL);
    lv_obj_set_style_bg_color(ok, t->accent, 0);
    lv_obj_t *ok_label = lv_obj_get_child(ok, 0);
    if (ok_label) {
        lv_obj_set_style_text_color(ok_label, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_set_pos(ok, 300, 164);

    lv_obj_t *kb = lv_keyboard_create(s_mask);
    lv_obj_set_size(kb, 480, 300);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

static void ap_clicked(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_ap_count) {
        return;
    }
    const net_wifi_ap_t *ap = &s_aps[index];
    if (ap->authmode == 0) { /* WIFI_AUTH_OPEN */
        connect_with(ap->ssid, "");
    } else {
        password_modal(ap->ssid);
    }
}

static void rebuild_list(void)
{
    lv_obj_clean(s_list);

    if (s_scan_running) {
        lv_obj_t *label = ui_label(s_list, "正在扫描附近 WiFi…", fonts_ui(), ui_theme()->muted);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(label, 40, 0);
        return;
    }

    if (s_ap_count == 0) {
        lv_obj_t *label = ui_label(s_list, "没有扫描到热点\n（仅支持 2.4G，可点右上角重试）",
                                   fonts_ui(), ui_theme()->muted);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, 6, 0);
        lv_obj_set_style_pad_top(label, 40, 0);
        return;
    }

    const ui_theme_t *t = ui_theme();
    for (int i = 0; i < s_ap_count; i++) {
        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_size(btn, LV_PCT(100), 84);
        lv_obj_set_style_bg_color(btn, t->panel, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t *name = ui_label(btn, s_aps[i].ssid, fonts_ui(), t->fg);
        lv_obj_set_pos(name, 18, 8);
        lv_obj_set_width(name, 300);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        char meta[64];
        snprintf(meta, sizeof(meta), "%d dBm · %s", s_aps[i].rssi,
                 s_aps[i].authmode == 0 ? "开放" : "加密");
        lv_obj_t *sub = ui_label(btn, meta, fonts_ui(), t->muted);
        lv_obj_set_pos(sub, 18, 46);

        lv_obj_add_event_cb(btn, ap_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void scan_task(void *arg)
{
    (void)arg;
    s_scan_running = true;
    s_scan_done = false;
    s_ap_count = net_wifi_scan(s_aps, MAX_APS);
    s_scan_running = false;
    s_scan_done = true;
    vTaskDelete(NULL);
}

static void start_scan(void)
{
    if (s_scan_running) {
        return;
    }
    s_scan_done = false;
    s_scan_running = true;
    rebuild_list();
    xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 4, NULL);
}

static void rescan_cb(lv_event_t *e)
{
    (void)e;
    start_scan();
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    net_wifi_disconnect();
    ui_toast("已清除 WiFi 配置");
    rebuild_list();
    start_scan();
}

static void update_status(void)
{
    if (!s_status) {
        return;
    }
    char buf[160];
    if (net_wifi_is_connected()) {
        char ssid[40] = {0};
        net_wifi_get_ssid(ssid, sizeof(ssid));
        snprintf(buf, sizeof(buf), "已连接：%s\nIP %s", ssid, net_wifi_get_ip());
    } else if (net_wifi_has_credentials()) {
        char ssid[40] = {0};
        net_wifi_get_ssid(ssid, sizeof(ssid));
        snprintf(buf, sizeof(buf), "正在连接：%s …", ssid);
    } else {
        snprintf(buf, sizeof(buf), "未连接\n点击下方热点进行连接");
    }
    lv_label_set_text(s_status, buf);

    if (s_hint) {
        lv_label_set_text(s_hint, storage_ready() ? "也可在 SD 卡根目录放 wifi.txt 配置" : "SD 卡不可用");
    }
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;
    update_status();
    if (s_scan_done) {
        s_scan_done = false;
        rebuild_list();
    }
}

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
    s_screen = NULL;
    s_list = NULL;
    s_status = NULL;
    s_hint = NULL;
    if (s_poll) {
        lv_timer_delete(s_poll);
        s_poll = NULL;
    }
}

void ui_wifi_show(void)
{
    const ui_theme_t *t = ui_theme();
    close_modal();
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }

    s_screen = ui_screen_new();
    lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *back = ui_button(s_screen, "返回", 84, 46, back_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "WiFi", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    lv_obj_t *rescan = ui_button(s_screen, "重新扫描", 140, 46, rescan_cb, NULL);
    lv_obj_align(rescan, LV_ALIGN_TOP_RIGHT, -20, 20);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 448, 96);
    lv_obj_set_pos(card, 16, 84);
    lv_obj_set_style_bg_color(card, t->panel, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_status = ui_label(card, "", fonts_ui(), t->fg);
    lv_obj_set_width(s_status, 280);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *forget = ui_button(card, "断开", 96, 44, forget_cb, NULL);
    lv_obj_align(forget, LV_ALIGN_RIGHT_MID, 0, 0);

    s_hint = ui_label(s_screen, "", fonts_ui(), t->muted);
    lv_obj_set_pos(s_hint, 20, 188);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, 448, 548);
    lv_obj_set_pos(s_list, 16, 224);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    update_status();
    s_scan_running = false;
    s_scan_done = false;
    rebuild_list();
    s_poll = lv_timer_create(poll_cb, 400, NULL);
    lv_screen_load(s_screen);

    start_scan();
}
