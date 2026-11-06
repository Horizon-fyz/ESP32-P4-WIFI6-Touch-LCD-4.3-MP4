#include "toc.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "reader.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_tab_chapters;
static lv_obj_t *s_tab_marks;
static int s_tab; /* 0 章节 1 书签 */

void ui_toc_rebuild(void);

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_reader_show_current();
}

static void chapter_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    ui_reader_goto_chapter(index);
}

static void bookmark_cb(lv_event_t *e)
{
    uint32_t offset = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ui_reader_goto_offset(offset);
}

static void clear_marks_cb(lv_event_t *e)
{
    (void)e;
    book_t *book = ui_reader_book();
    if (!book) {
        return;
    }
    settings_bookmarks_clear(book->path);
    ui_toast("书签已清空");
    s_tab = 1;
    ui_toc_rebuild();
}

static lv_obj_t *make_row(const char *text, const char *right, bool highlight, lv_event_cb_t cb, void *user)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *btn = lv_button_create(s_list);
    lv_obj_set_size(btn, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(btn, t->panel, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, fonts_ui(), 0);
    lv_obj_set_style_text_color(label, highlight ? t->accent : t->fg, 0);
    lv_obj_set_pos(label, 18, 22);
    lv_obj_set_width(label, 340);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);

    if (right) {
        lv_obj_t *r = lv_label_create(btn);
        lv_label_set_text(r, right);
        lv_obj_set_style_text_font(r, fonts_ui(), 0);
        lv_obj_set_style_text_color(r, t->muted, 0);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, -18, 0);
    }

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    }
    return btn;
}

void ui_toc_rebuild(void)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);
    book_t *book = ui_reader_book();
    if (!book) {
        return;
    }

    const ui_theme_t *t = ui_theme();
    int current = ui_reader_current_chapter();

    if (s_tab == 0) {
        for (int i = 0; i < book->chapter_count; i++) {
            make_row(book->chapters[i].title, NULL, i == current, chapter_cb, (void *)(intptr_t)i);
        }
    } else {
        reader_bookmark_t marks[16];
        int n = settings_bookmarks_get(book->path, marks, 16);
        if (n == 0) {
            lv_obj_t *hint = ui_label(s_list, "还没有书签\n\n阅读时点开菜单，选择“书签”即可添加", fonts_ui(), t->muted);
            lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(hint, LV_PCT(100));
            lv_obj_set_style_pad_top(hint, 40, 0);
            return;
        }
        for (int i = 0; i < n; i++) {
            make_row(marks[i].title, "跳转", false, bookmark_cb, (void *)(uintptr_t)marks[i].offset);
        }
    }
}

static void set_tab(int tab)
{
    s_tab = tab;
    const ui_theme_t *t = ui_theme();
    lv_obj_t *a = lv_obj_get_child(s_tab_chapters, 0);
    lv_obj_t *b = lv_obj_get_child(s_tab_marks, 0);
    if (a) {
        lv_obj_set_style_text_color(a, tab == 0 ? t->accent : t->muted, 0);
    }
    if (b) {
        lv_obj_set_style_text_color(b, tab == 1 ? t->accent : t->muted, 0);
    }
    lv_obj_set_style_bg_color(s_tab_chapters, tab == 0 ? t->bg : t->panel, 0);
    lv_obj_set_style_bg_color(s_tab_marks, tab == 1 ? t->bg : t->panel, 0);
    ui_toc_rebuild();
}

static void tab_chapters_cb(lv_event_t *e)
{
    (void)e;
    set_tab(0);
}

static void tab_marks_cb(lv_event_t *e)
{
    (void)e;
    set_tab(1);
}

void ui_toc_show(void)
{
    book_t *book = ui_reader_book();
    if (!book) {
        return;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }

    const ui_theme_t *t = ui_theme();
    s_screen = ui_screen_new();

    lv_obj_t *back = ui_button(s_screen, "返回", 84, 46, back_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, book->title, fonts_ui(), t->fg);
    lv_obj_set_pos(title, 116, 28);
    lv_obj_set_width(title, 348);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);

    s_tab_chapters = ui_button(s_screen, "章节", 180, 52, tab_chapters_cb, NULL);
    lv_obj_set_pos(s_tab_chapters, 40, 80);
    s_tab_marks = ui_button(s_screen, "书签", 180, 52, tab_marks_cb, NULL);
    lv_obj_set_pos(s_tab_marks, 260, 80);

    lv_obj_t *clear = ui_button(s_screen, "清空书签", 120, 44, clear_marks_cb, NULL);
    lv_obj_align(clear, LV_ALIGN_TOP_RIGHT, -16, 144);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, 448, 540);
    lv_obj_set_pos(s_list, 16, 200);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    s_tab = 0;
    set_tab(0);
    lv_screen_load(s_screen);
}
