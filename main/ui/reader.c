#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/fonts.h"
#include "esp_log.h"
#include "layout/layout.h"
#include "library.h"
#include "storage/storage.h"
#include "theme.h"
#include "toc.h"
#include "widgets.h"

#define SCREEN_W 480
#define SCREEN_H 800
#define TEXT_X 26
#define TEXT_Y 54
#define TEXT_W 428
#define TEXT_H 668

static const char *TAG = "reader";

static lv_obj_t *s_screen;
static lv_obj_t *s_page_label;
static lv_obj_t *s_top_title;
static lv_obj_t *s_top_right;
static lv_obj_t *s_bottom_left;
static lv_obj_t *s_bottom_right;
static lv_obj_t *s_menu;

static book_t s_book;
static bool s_open;
static bool s_pg_valid;
static int s_chapter;
static int s_page;
static bool s_menu_open;
static char s_path[192];
static pagination_t s_pg;

void ui_reader_init(void)
{
    s_screen = NULL;
    s_open = false;
    s_pg_valid = false;
}

book_t *ui_reader_book(void)
{
    return s_open ? &s_book : NULL;
}

/* -------------------------------------------------------------------------- */

static void save_progress_now(void)
{
    if (!s_open || !s_pg_valid) {
        return;
    }
    uint32_t off = s_book.chapters[s_chapter].offset + pagination_page_offset(&s_pg, s_page);
    uint16_t pct = s_book.length ? (uint16_t)((uint64_t)off * 1000u / s_book.length) : 0;
    settings_set_progress(s_path, off, pct);

    reader_cfg_t *cfg = settings_get();
    snprintf(cfg->last_book, sizeof(cfg->last_book), "%s", s_path);
    cfg->last_offset = off;
    settings_save();
}

void ui_reader_save_progress(void)
{
    save_progress_now();
}

static void render_page(void)
{
    if (!s_pg_valid) {
        return;
    }
    const ui_theme_t *t = ui_theme();
    char *txt = pagination_page_text(&s_pg, s_book.text, s_page);
    lv_label_set_text(s_page_label, txt ? txt : "");
    free(txt);
    lv_obj_set_style_text_color(s_page_label, t->fg, 0);

    lv_label_set_text(s_top_title, s_book.title);

    int pages = pagination_page_count(&s_pg);
    uint32_t off = s_book.chapters[s_chapter].offset + pagination_page_offset(&s_pg, s_page);
    int pct = s_book.length ? (int)((uint64_t)off * 100 / s_book.length) : 0;

    char buf[64];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_top_right, buf);

    lv_label_set_text(s_bottom_left, s_book.chapters[s_chapter].title);

    snprintf(buf, sizeof(buf), "%d / %d 页", s_page + 1, pages > 0 ? pages : 1);
    lv_label_set_text(s_bottom_right, buf);

    save_progress_now();
}

static void load_chapter(int index, uint32_t offset)
{
    if (!s_open) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= s_book.chapter_count) {
        index = s_book.chapter_count - 1;
    }
    s_chapter = index;

    const book_chapter_t *ch = &s_book.chapters[s_chapter];
    const lv_font_t *font = fonts_select_reader(settings_get()->font_idx);

    pagination_free(&s_pg);
    s_pg_valid = pagination_build(&s_pg, s_book.text, ch->offset, ch->offset + ch->length, font, TEXT_W,
                                  TEXT_H, settings_get()->line_space);
    if (!s_pg_valid) {
        ESP_LOGE(TAG, "分页失败: 第 %d 章", index);
        lv_label_set_text(s_page_label, "（本章内容为空）");
        return;
    }
    s_page = pagination_page_of_offset(&s_pg, offset);
    render_page();
}

static void next_page(void)
{
    if (!s_open || !s_pg_valid) {
        return;
    }
    if (s_page + 1 < pagination_page_count(&s_pg)) {
        s_page++;
        render_page();
        return;
    }
    if (s_chapter + 1 < s_book.chapter_count) {
        load_chapter(s_chapter + 1, s_book.chapters[s_chapter + 1].offset);
        return;
    }
    ui_toast("已经是最后一页了");
}

static void prev_page(void)
{
    if (!s_open || !s_pg_valid) {
        return;
    }
    if (s_page > 0) {
        s_page--;
        render_page();
        return;
    }
    if (s_chapter > 0) {
        const book_chapter_t *prev = &s_book.chapters[s_chapter - 1];
        load_chapter(s_chapter - 1, prev->offset + prev->length);
        return;
    }
    ui_toast("已经是第一页了");
}

/* -------------------------------------------------------------------------- */

static void menu_show(bool show)
{
    if (!s_menu) {
        return;
    }
    s_menu_open = show;
    if (show) {
        lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

static void toggle_menu(void)
{
    menu_show(!s_menu_open);
}

static lv_point_t s_press_point;
static bool s_swiped;

static void left_cb(lv_event_t *e)
{
    (void)e;
    if (s_swiped) {
        s_swiped = false;
        return;
    }
    prev_page();
}

static void right_cb(lv_event_t *e)
{
    (void)e;
    if (s_swiped) {
        s_swiped = false;
        return;
    }
    next_page();
}

static void center_cb(lv_event_t *e)
{
    (void)e;
    if (s_swiped) {
        s_swiped = false;
        return;
    }
    toggle_menu();
}

/* BSP 的触摸驱动没有开启 LVGL 手势识别，这里用按下/抬起坐标自行判断滑动 */
static void zone_pressed_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &s_press_point);
    }
    s_swiped = false;
}

static void zone_released_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dx = p.x - s_press_point.x;
    if (dx <= -60) {
        s_swiped = true;
        next_page();
    } else if (dx >= 60) {
        s_swiped = true;
        prev_page();
    }
}

static void toc_cb(lv_event_t *e)
{
    (void)e;
    menu_show(false);
    ui_toc_show();
}

static void font_smaller_cb(lv_event_t *e)
{
    (void)e;
    reader_cfg_t *cfg = settings_get();
    if (cfg->font_idx == 0) {
        ui_toast("已经是最小字号");
        return;
    }
    cfg->font_idx--;
    settings_save();
    ui_reader_reapply();
}

static void font_larger_cb(lv_event_t *e)
{
    (void)e;
    reader_cfg_t *cfg = settings_get();
    if (cfg->font_idx + 1 >= fonts_count()) {
        ui_toast("已经是最大字号");
        return;
    }
    cfg->font_idx++;
    settings_save();
    ui_reader_reapply();
}

static void theme_cb(lv_event_t *e)
{
    (void)e;
    int next = (ui_theme_index() + 1) % UI_THEME_COUNT;
    ui_theme_set_index(next);
    settings_get()->theme = (uint8_t)next;
    settings_save();
    ui_reader_reapply();
    ui_toast("主题：%s", ui_theme_name(next));
}

static void bookmark_cb(lv_event_t *e)
{
    (void)e;
    if (!s_open || !s_pg_valid) {
        return;
    }
    uint32_t off = s_book.chapters[s_chapter].offset + pagination_page_offset(&s_pg, s_page);
    char title[96];
    snprintf(title, sizeof(title), "%s · %d%%", s_book.chapters[s_chapter].title,
             s_book.length ? (int)((uint64_t)off * 100 / s_book.length) : 0);
    if (settings_bookmark_add(s_path, off, title)) {
        ui_toast("已添加书签");
    } else {
        ui_toast("书签添加失败");
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    menu_show(false);
    save_progress_now();
    ui_library_show();
}

/* -------------------------------------------------------------------------- */

static lv_obj_t *create_zone(lv_obj_t *parent, int x, int w, lv_event_cb_t cb)
{
    lv_obj_t *zone = lv_obj_create(parent);
    lv_obj_set_pos(zone, x, 0);
    lv_obj_set_size(zone, w, TEXT_H + 20);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(zone, zone_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(zone, zone_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(zone, cb, LV_EVENT_CLICKED, NULL);
    return zone;
}

static void build_screen(void)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *old = s_screen;

    s_screen = ui_screen_new();

    s_top_title = ui_label(s_screen, s_book.title, fonts_ui(), t->muted);
    lv_obj_set_pos(s_top_title, 20, 12);
    lv_obj_set_width(s_top_title, 360);
    lv_label_set_long_mode(s_top_title, LV_LABEL_LONG_MODE_DOTS);

    s_top_right = ui_label(s_screen, "0%", fonts_ui(), t->muted);
    lv_obj_align(s_top_right, LV_ALIGN_TOP_RIGHT, -20, 12);

    lv_obj_t *line = lv_obj_create(s_screen);
    lv_obj_set_size(line, 480, 1);
    lv_obj_set_pos(line, 0, 46);
    lv_obj_set_style_bg_color(line, t->line, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    s_page_label = lv_label_create(s_screen);
    lv_obj_set_pos(s_page_label, TEXT_X, TEXT_Y);
    lv_obj_set_size(s_page_label, TEXT_W, TEXT_H);
    lv_label_set_long_mode(s_page_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(s_page_label, fonts_current_reader(), 0);
    lv_obj_set_style_text_color(s_page_label, t->fg, 0);
    lv_obj_set_style_text_line_space(s_page_label, settings_get()->line_space, 0);

    create_zone(s_screen, 0, 120, left_cb);
    create_zone(s_screen, 120, 240, center_cb);
    create_zone(s_screen, 360, 120, right_cb);

    s_bottom_left = ui_label(s_screen, "", fonts_ui(), t->muted);
    lv_obj_set_pos(s_bottom_left, 20, 740);
    lv_obj_set_width(s_bottom_left, 300);
    lv_label_set_long_mode(s_bottom_left, LV_LABEL_LONG_MODE_DOTS);

    s_bottom_right = ui_label(s_screen, "", fonts_ui(), t->muted);
    lv_obj_align(s_bottom_right, LV_ALIGN_BOTTOM_RIGHT, -20, -14);

    /* 菜单 */
    s_menu = lv_obj_create(s_screen);
    lv_obj_set_size(s_menu, 480, 250);
    lv_obj_set_pos(s_menu, 0, SCREEN_H - 250);
    lv_obj_set_style_bg_color(s_menu, t->panel, 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_menu, 0, 0);
    lv_obj_set_style_border_width(s_menu, 1, 0);
    lv_obj_set_style_border_side(s_menu, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_menu, t->line, 0);
    lv_obj_set_style_pad_all(s_menu, 0, 0);
    lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);

    const char *texts[6] = {"目录", "字号 -", "字号 +", "主题", "书签", "书架"};
    lv_event_cb_t cbs[6] = {toc_cb, font_smaller_cb, font_larger_cb, theme_cb, bookmark_cb, back_cb};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *btn = ui_button(s_menu, texts[i], 138, 92, cbs[i], NULL);
        lv_obj_set_pos(btn, 12 + (i % 3) * 150, 16 + (i / 3) * 110);
    }

    menu_show(false);

    lv_screen_load(s_screen);
    if (old && old != s_screen) {
        lv_obj_delete(old);
    }
    s_menu_open = false;
}

void ui_reader_reapply(void)
{
    if (!s_open) {
        return;
    }
    uint32_t off = 0;
    if (s_pg_valid) {
        off = s_book.chapters[s_chapter].offset + pagination_page_offset(&s_pg, s_page);
    } else {
        off = s_book.chapters[s_chapter].offset;
    }

    pagination_free(&s_pg);
    s_pg_valid = false;

    build_screen();
    load_chapter(s_chapter, off);
}

void ui_reader_goto_chapter(int chapter)
{
    if (!s_open) {
        return;
    }
    load_chapter(chapter, s_book.chapters[chapter].offset);
    menu_show(false);
    lv_screen_load(s_screen);
}

void ui_reader_goto_offset(uint32_t offset)
{
    if (!s_open) {
        return;
    }
    int chapter = book_chapter_at(&s_book, offset);
    load_chapter(chapter, offset);
    menu_show(false);
    lv_screen_load(s_screen);
}

void ui_reader_show_current(void)
{
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

int ui_reader_current_chapter(void)
{
    return s_open ? s_chapter : 0;
}

void ui_reader_open(const char *path)
{
    if (s_open && strcmp(path, s_path) == 0) {
        lv_screen_load(s_screen);
        return;
    }
    if (s_open) {
        save_progress_now();
        book_close(&s_book);
        s_open = false;
        pagination_free(&s_pg);
        s_pg_valid = false;
    }

    if (book_open(path, &s_book) != ESP_OK) {
        ui_toast("无法打开这本书");
        return;
    }
    s_open = true;
    snprintf(s_path, sizeof(s_path), "%s", path);

    settings_get()->font_idx = settings_get()->font_idx >= fonts_count() ? 0 : settings_get()->font_idx;

    uint32_t offset = settings_progress_offset(path);
    if (offset >= s_book.length) {
        offset = 0;
    }
    s_chapter = book_chapter_at(&s_book, offset);

    build_screen();
    load_chapter(s_chapter, offset);

    ESP_LOGI(TAG, "打开: %s（%d 章）", s_book.title, s_book.chapter_count);
}
