#include "settings_screen.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "ble_screen.h"
#include "bsp/esp-bsp.h"
#include "clock_screen.h"
#include "home.h"
#include "library.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"
#include "wifi.h"
#include "wifi_screen.h"

#define APP_VERSION "2.0.0"

static lv_obj_t *s_screen;
static lv_obj_t *s_font_value;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_upload_url;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_home_refresh();
    ui_home_show();
}

static void wifi_page_cb(lv_event_t *e)
{
    (void)e;
    ui_wifi_show();
}

static void ble_page_cb(lv_event_t *e)
{
    (void)e;
    ui_ble_show();
}

static void clock_page_cb(lv_event_t *e)
{
    (void)e;
    ui_clock_show();
}

/*
 * 卡片一律用「宽度 100% + 高度自适应内容 + 纵向 flex」。
 * 不要再用固定高度 + lv_obj_align：中文字体 line_height 是 43px（22px 字号），
 * 按字面字号估算的固定高度会让标题与控件重叠、内容溢出卡片。
 */
static lv_obj_t *card(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_style_bg_color(c, t->panel, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* 卡片内部的横向排布容器（无背景、无内边距） */
static lv_obj_t *hrow(lv_obj_t *parent, lv_flex_align_t main_align)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *card_title(lv_obj_t *card_obj, const char *text)
{
    return ui_label(card_obj, text, fonts_ui(), ui_theme()->muted);
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = (int)lv_slider_get_value(slider);
    bsp_display_brightness_set(value);
    settings_get()->brightness = (uint8_t)value;
    settings_save();
}

static void update_font_label(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d px", fonts_size_px(settings_get()->font_idx));
    lv_label_set_text(s_font_value, buf);
}

static void font_minus_cb(lv_event_t *e)
{
    (void)e;
    reader_cfg_t *cfg = settings_get();
    if (cfg->font_idx > 0) {
        cfg->font_idx--;
        settings_save();
        update_font_label();
    }
}

static void font_plus_cb(lv_event_t *e)
{
    (void)e;
    reader_cfg_t *cfg = settings_get();
    if (cfg->font_idx + 1 < fonts_count()) {
        cfg->font_idx++;
        settings_save();
        update_font_label();
    }
}

static void theme_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_theme_set_index(idx);
    settings_get()->theme = (uint8_t)idx;
    settings_save();
    /* 首页和书架是缓存的常驻界面，必须重建才能应用新配色 */
    ui_home_reset();
    ui_library_reset();
    ui_settings_show();
}

static void update_wifi_label(void)
{
    if (net_wifi_is_connected()) {
        char ssid[40] = {0};
        net_wifi_get_ssid(ssid, sizeof(ssid));
        char buf[128];
        snprintf(buf, sizeof(buf), "已连接 %s · %s", ssid, net_wifi_get_ip());
        lv_label_set_text(s_wifi_status, buf);

        char url[64];
        snprintf(url, sizeof(url), "http://%s/", net_wifi_get_ip());
        lv_label_set_text(s_upload_url, url);
    } else if (net_wifi_has_credentials()) {
        lv_label_set_text(s_wifi_status, "未连接（检查 2.4G WiFi 是否可用）");
        lv_label_set_text(s_upload_url, "连接 WiFi 后可用");
    } else {
        lv_label_set_text(s_wifi_status, "未配置：把 wifi.txt 放到 SD 卡根目录");
        lv_label_set_text(s_upload_url, "连接 WiFi 后可用");
    }
}

void ui_settings_show(void)
{
    const ui_theme_t *t = ui_theme();
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_screen = ui_screen_new();

    lv_obj_t *back = ui_button(s_screen, "返回", 84, 46, back_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "设置", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    lv_obj_t *content = lv_obj_create(s_screen);
    lv_obj_set_size(content, 448, 700);
    lv_obj_set_pos(content, 16, 84);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_right(content, 8, 0);   /* 给右侧滚动条留位，避免压住卡片 */
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    /* 亮度 */
    lv_obj_t *c1 = card(content);
    card_title(c1, "屏幕亮度");
    lv_obj_t *slider = lv_slider_create(c1);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_height(slider, 12);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, settings_get()->brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 字号 */
    lv_obj_t *c2 = card(content);
    card_title(c2, "正文字号");
    lv_obj_t *font_row = hrow(c2, LV_FLEX_ALIGN_SPACE_BETWEEN);
    ui_button(font_row, "A-", 96, 52, font_minus_cb, NULL);
    s_font_value = ui_label(font_row, "26 px", fonts_ui(), t->fg);
    ui_button(font_row, "A+", 96, 52, font_plus_cb, NULL);
    update_font_label();

    /* 主题 */
    lv_obj_t *c3 = card(content);
    card_title(c3, "阅读主题");
    lv_obj_t *theme_row = hrow(c3, LV_FLEX_ALIGN_SPACE_BETWEEN);
    for (int i = 0; i < UI_THEME_COUNT; i++) {
        lv_obj_t *b = ui_button(theme_row, ui_theme_name(i), 120, 52, theme_cb,
                                (void *)(intptr_t)i);
        if (i == ui_theme_index()) {
            lv_obj_set_style_bg_color(b, t->accent, 0);
            lv_obj_t *l = lv_obj_get_child(b, 0);
            if (l) {
                lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
            }
        }
    }

    /* 网络与连接 */
    lv_obj_t *c4 = card(content);
    card_title(c4, "网络与连接");
    s_wifi_status = ui_label(c4, "", fonts_ui(), t->fg);
    lv_obj_set_width(s_wifi_status, LV_PCT(100));
    lv_label_set_long_mode(s_wifi_status, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *hint = ui_label(c4, "点击「WiFi」扫描附近热点并输入密码；\n也可以在 SD 卡根目录放 wifi.txt。",
                              fonts_ui(), t->muted);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_line_space(hint, 4, 0);

    lv_obj_t *net_row = hrow(c4, LV_FLEX_ALIGN_SPACE_BETWEEN);
    ui_button(net_row, "WiFi", 132, 52, wifi_page_cb, NULL);
    ui_button(net_row, "蓝牙", 132, 52, ble_page_cb, NULL);
    ui_button(net_row, "时钟", 132, 52, clock_page_cb, NULL);

    /* 网页上传 */
    lv_obj_t *c5 = card(content);
    card_title(c5, "网页上传书库");
    s_upload_url = ui_label(c5, "", fonts_get(1), t->accent);
    lv_obj_set_width(s_upload_url, LV_PCT(100));

    /* 存储 */
    uint64_t free_b = 0;
    uint64_t total_b = 0;
    storage_free_space(&free_b, &total_b);
    lv_obj_t *c6 = card(content);
    card_title(c6, "存储空间");
    char sbuf[96];
    if (total_b > 0) {
        snprintf(sbuf, sizeof(sbuf), "可用 %.1f GB / 共 %.1f GB", free_b / 1073741824.0,
                 total_b / 1073741824.0);
    } else {
        snprintf(sbuf, sizeof(sbuf), "SD 卡不可用（需 FAT32 格式）");
    }
    lv_obj_t *storage_label = ui_label(c6, sbuf, fonts_ui(), t->fg);
    lv_obj_set_width(storage_label, LV_PCT(100));

    /* 关于 */
    lv_obj_t *c7 = card(content);
    card_title(c7, "关于");
    lv_obj_t *about = ui_label(c7,
                               "MP4 阅读器 v" APP_VERSION "\n"
                               "ESP32-P4-WIFI6-Touch-LCD-4.3",
                               fonts_ui(), t->fg);
    lv_obj_set_width(about, LV_PCT(100));
    lv_obj_set_style_text_line_space(about, 6, 0);

    update_wifi_label();
    lv_screen_load(s_screen);
}
