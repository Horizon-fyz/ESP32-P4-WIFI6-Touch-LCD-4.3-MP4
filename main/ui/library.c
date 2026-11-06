#include "library.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "esp_log.h"
#include "home.h"
#include "reader.h"
#include "settings_screen.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"
#include "wifi.h"

#define MAX_BOOKS 64

static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_status;

static lib_book_t s_books[MAX_BOOKS];
static int s_count;
static char s_delete_path[192];

static void on_delete_confirmed(void *user)
{
    (void)user;
    if (storage_delete(s_delete_path) == ESP_OK) {
        ui_toast("已删除");
    } else {
        ui_toast("删除失败");
    }
    ui_library_refresh();
}

static bool s_suppress_click;

static void item_pressed(lv_event_t *e)
{
    (void)e;
    s_suppress_click = false;
}

static void item_long_pressed(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_count) {
        return;
    }
    s_suppress_click = true;
    snprintf(s_delete_path, sizeof(s_delete_path), "%s", s_books[index].path);
    char msg[160];
    snprintf(msg, sizeof(msg), "删除《%s》？", s_books[index].title);
    ui_confirm(msg, "删除", on_delete_confirmed, NULL);
}

static void item_clicked(lv_event_t *e)
{
    if (s_suppress_click) {
        s_suppress_click = false;
        return;
    }
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_count) {
        return;
    }
    ui_reader_open(s_books[index].path);
}

static void make_item(lv_obj_t *parent, const lib_book_t *b, int index)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 118);
    lv_obj_set_style_bg_color(btn, t->panel, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *title = lv_label_create(btn);
    lv_label_set_text(title, b->title);
    lv_obj_set_style_text_font(title, fonts_get(1), 0);
    lv_obj_set_style_text_color(title, t->fg, 0);
    lv_obj_set_width(title, 400);
    /* 固定单行高度，长书名才会用省略号截断而不是换行压住下面的信息 */
    lv_obj_set_height(title, 50);
    lv_obj_set_pos(title, 18, 6);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);

    char meta[96];
    snprintf(meta, sizeof(meta), "%s · %.1f MB · 已读 %d%%", b->type == BOOK_TYPE_EPUB ? "EPUB" : "TXT",
             b->size / 1048576.0, b->percent / 10);
    lv_obj_t *sub = lv_label_create(btn);
    lv_label_set_text(sub, meta);
    lv_obj_set_style_text_font(sub, fonts_ui(), 0);
    lv_obj_set_style_text_color(sub, t->muted, 0);
    lv_obj_set_pos(sub, 18, 60);

    if (b->percent > 0) {
        lv_obj_t *bar_bg = lv_obj_create(btn);
        lv_obj_set_size(bar_bg, 412, 3);
        lv_obj_set_pos(bar_bg, 18, 104);
        lv_obj_set_style_bg_color(bar_bg, t->line, 0);
        lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar_bg, 0, 0);
        lv_obj_set_style_radius(bar_bg, 0, 0);
        lv_obj_remove_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *bar = lv_obj_create(btn);
        int w = (int)(412u * (b->percent > 1000 ? 1000 : b->percent) / 1000u);
        lv_obj_set_size(bar, w > 0 ? w : 1, 3);
        lv_obj_set_pos(bar, 18, 104);
        lv_obj_set_style_bg_color(bar, t->accent, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_add_event_cb(btn, item_pressed, LV_EVENT_PRESSED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(btn, item_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(btn, item_long_pressed, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)index);
}

static void rebuild(void)
{
    lv_obj_clean(s_list);
    s_count = storage_scan(s_books, MAX_BOOKS);

    if (!storage_ready()) {
        lv_obj_t *hint = ui_label(s_list,
                                  "SD 卡不可用\n\n请确认已插入 Micro SD 卡，\n"
                                  "且分区为 FAT32（exFAT / NTFS 无法识别）\n\n"
                                  "插好后点击右上角“刷新”",
                                  fonts_ui(), ui_theme()->muted);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_pad_top(hint, 40, 0);
        return;
    }

    if (s_count == 0) {
        lv_obj_t *hint = ui_label(s_list,
                                  "书库是空的\n\n把 .txt / .epub 放到 SD 卡\n/books 目录，\n"
                                  "或用手机/电脑打开底部网址上传",
                                  fonts_ui(), ui_theme()->muted);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_pad_top(hint, 40, 0);
        return;
    }

    for (int i = 0; i < s_count; i++) {
        make_item(s_list, &s_books[i], i);
    }
}

static void update_status(void)
{
    char buf[128];
    if (net_wifi_is_connected()) {
        snprintf(buf, sizeof(buf), "WiFi 已连接 · %s", net_wifi_get_ip());
    } else if (net_wifi_has_credentials()) {
        snprintf(buf, sizeof(buf), "WiFi 连接中…");
    } else {
        snprintf(buf, sizeof(buf), "WiFi 未配置（见设置）");
    }
    lv_label_set_text(s_status, buf);
}

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    if (!storage_ready()) {
        storage_init();
    }
    rebuild();
    update_status();
    ui_toast("已刷新");
}

static void settings_cb(lv_event_t *e)
{
    (void)e;
    ui_settings_show();
}

static void home_cb(lv_event_t *e)
{
    (void)e;
    ui_home_show();
}

void ui_library_refresh(void)
{
    if (!s_list) {
        return;
    }
    rebuild();
    update_status();
}

void ui_library_reset(void)
{
    /* 活动屏幕不能直接删，否则 LVGL 的屏幕指针会悬空 */
    if (!s_screen || lv_screen_active() == s_screen) {
        return;
    }
    lv_obj_delete(s_screen);
    s_screen = NULL;
    s_list = NULL;
    s_status = NULL;
}

void ui_library_show(void)
{
    if (s_screen) {
        update_status();
        lv_screen_load(s_screen);
        return;
    }

    const ui_theme_t *t = ui_theme();
    s_screen = ui_screen_new();

    lv_obj_t *title = ui_label(s_screen, "书库", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 24, 18);

    s_status = ui_label(s_screen, "", fonts_ui(), t->muted);
    lv_obj_set_pos(s_status, 24, 58);
    lv_obj_set_width(s_status, 244);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *refresh = ui_button(s_screen, "刷新", 80, 46, refresh_cb, NULL);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -112, 20);

    lv_obj_t *home = ui_button(s_screen, "首页", 80, 46, home_cb, NULL);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -204, 20);

    lv_obj_t *settings = ui_button(s_screen, "设置", 80, 46, settings_cb, NULL);
    lv_obj_align(settings, LV_ALIGN_TOP_RIGHT, -20, 20);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, 448, 636);
    lv_obj_set_pos(s_list, 16, 100);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 10, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *footer = ui_label(s_screen, "SD:/books · TXT / EPUB · 长按删除", fonts_ui(), t->muted);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -16);

    rebuild();
    update_status();
    lv_screen_load(s_screen);
}
