#include "ble_screen.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "audio/player.h"
#include "ble.h"
#include "home.h"
#include "theme.h"
#include "widgets.h"

#define MAX_SHOWN 32

static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_state;
static lv_obj_t *s_phone_text;
static lv_timer_t *s_poll;

static ble_device_t s_devices[MAX_SHOWN];
static int s_count;
static uint32_t s_last_revision = 0xFFFFFFFFu;
static bool s_scan_started;

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    ble_stop();
    ui_home_show();
}

static void addr_text(const ble_device_t *d, char *out, size_t n)
{
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", d->addr[5], d->addr[4], d->addr[3],
             d->addr[2], d->addr[1], d->addr[0]);
}

static void dev_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_count) {
        return;
    }
    if (ble_connect(&s_devices[index]) == ESP_OK) {
        ui_toast("正在连接 %s…", s_devices[index].name);
    } else {
        ui_toast("连接失败");
    }
}

static void rebuild(void)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);
    const ui_theme_t *t = ui_theme();

    if (s_count == 0) {
        lv_obj_t *hint = ui_label(s_list, "暂未发现 BLE 设备\n\n请确认目标设备已开启广播",
                                  fonts_ui(), t->muted);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(hint, 6, 0);
        lv_obj_set_style_pad_top(hint, 24, 0);
        return;
    }

    for (int i = 0; i < s_count; i++) {
        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_size(btn, LV_PCT(100), 92);
        lv_obj_set_style_bg_color(btn, t->panel, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t *name = ui_label(btn, s_devices[i].name, fonts_ui(), t->fg);
        lv_obj_set_pos(name, 16, 2);
        lv_obj_set_width(name, 380);
        lv_obj_set_height(name, 43);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        char meta[64];
        char addr[24];
        addr_text(&s_devices[i], addr, sizeof(addr));
        snprintf(meta, sizeof(meta), "%s · %d dBm", addr, s_devices[i].rssi);
        lv_obj_t *sub = ui_label(btn, meta, fonts_ui(), t->muted);
        lv_obj_set_pos(sub, 16, 46);

        lv_obj_add_event_cb(btn, dev_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (!ble_is_ready()) {
        if (ble_start() != ESP_OK) {
            ui_toast("BLE 初始化失败");
        }
        return;
    }
    ble_scan_start();
    ui_toast("正在扫描 BLE 设备…");
}

static void disconnect_cb(lv_event_t *e)
{
    (void)e;
    ble_disconnect();
    ui_toast("已断开");
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_screen || lv_screen_active() != s_screen) {
        return;
    }

    if (s_state) {
        const char *state = ble_state_text();
        if (ble_phone_connected()) {
            lv_label_set_text(s_state, "手机已连接（可收发文本）");
        } else if (ble_is_connected()) {
            lv_label_set_text(s_state, "已连接外部 BLE 设备");
        } else {
            lv_label_set_text(s_state, state);
        }
    }

    if (s_phone_text) {
        char buf[96];
        snprintf(buf, sizeof(buf), "收到：%s", ble_phone_last_text()[0] ? ble_phone_last_text() : "（暂无）");
        lv_label_set_text(s_phone_text, buf);
    }

    if (ble_is_ready() && !s_scan_started) {
        s_scan_started = true;
        ble_scan_start();
    }

    uint32_t rev = ble_scan_revision();
    if (rev != s_last_revision) {
        s_last_revision = rev;
        s_count = ble_get_devices(s_devices, MAX_SHOWN);
        rebuild();
    }
}

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    s_screen = NULL;
    s_list = NULL;
    s_state = NULL;
    s_phone_text = NULL;
    if (s_poll) {
        lv_timer_delete(s_poll);
        s_poll = NULL;
    }
}

void ui_ble_show(void)
{
    /* BLE 与音频都要占 DMA 内存，进蓝牙页前先释放音频 */
    audio_shutdown();

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }

    const ui_theme_t *t = ui_theme();
    s_screen = ui_screen_new();
    lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *back = ui_button(s_screen, "首页", 84, 46, back_home_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "蓝牙 BLE", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 448, 116);
    lv_obj_set_pos(card, 16, 84);
    lv_obj_set_style_bg_color(card, t->panel, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_hint = ui_label(card, "本机名称：MP4-Reader", fonts_ui(), t->muted);
    lv_obj_set_pos(name_hint, 4, 0);

    s_state = ui_label(card, ble_state_text(), fonts_ui(), t->accent);
    lv_obj_set_pos(s_state, 4, 32);

    s_phone_text = ui_label(card, "收到：（暂无）", fonts_ui(), t->muted);
    lv_obj_set_width(s_phone_text, 280);
    lv_obj_set_pos(s_phone_text, 4, 62);
    lv_label_set_long_mode(s_phone_text, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *scan = ui_button(card, "扫描", 84, 44, scan_cb, NULL);
    lv_obj_align(scan, LV_ALIGN_TOP_RIGHT, 0, 4);

    lv_obj_t *disconnect = ui_button(card, "断开", 84, 44, disconnect_cb, NULL);
    lv_obj_align(disconnect, LV_ALIGN_BOTTOM_RIGHT, 0, -4);

    lv_obj_t *hint = ui_label(s_screen,
                              "手机用「BLE 调试助手」连接 MP4-Reader，\n"
                              "向 RX（6E400002…）写文本，设备会回显到 TX。",
                              fonts_ui(), t->muted);
    lv_obj_set_pos(hint, 22, 208);
    lv_obj_set_style_text_line_space(hint, 4, 0);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, 448, 520);
    lv_obj_set_pos(s_list, 16, 268);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    s_scan_started = false;
    s_last_revision = 0xFFFFFFFFu;
    s_count = 0;
    rebuild();

    if (!ble_is_ready()) {
        if (ble_start() != ESP_OK) {
            ui_toast("BLE 初始化失败（C6 固件可能不支持蓝牙）");
        }
    }

    if (!s_poll) {
        s_poll = lv_timer_create(poll_cb, 500, NULL);
    }
    lv_screen_load(s_screen);
}
