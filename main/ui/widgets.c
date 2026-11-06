#include "widgets.h"

#include <stdarg.h>
#include <stdio.h>

#include "app/fonts.h"
#include "theme.h"

static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_toast_timer = NULL;
static lv_obj_t *s_modal = NULL;

lv_obj_t *ui_screen_new(void)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, t->bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    return scr;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font ? font : fonts_ui(), 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb, void *user)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, t->panel, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, fonts_ui(), 0);
    lv_obj_set_style_text_color(label, t->fg, 0);
    lv_obj_center(label);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    }
    return btn;
}

/*
 * 关闭当前 toast。
 * 必须先删定时器再删对象：否则定时器到期时会去 delete 已经释放的对象，
 * 触发 use-after-free（Load access fault 直接 panic）。
 */
static void toast_close(void)
{
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_toast_timer = NULL;   /* 计时器已到期，先摘掉句柄，避免重复删除 */
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    lv_timer_delete(timer);
}

void ui_toast(const char *fmt, ...)
{
    const ui_theme_t *t = ui_theme();
    toast_close();

    char text[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    lv_obj_set_style_bg_color(box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_80, 0);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_hor(box, 20, 0);
    lv_obj_set_style_pad_ver(box, 12, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -60);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, fonts_ui(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(label, 380);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    s_toast = box;
    s_toast_timer = lv_timer_create(toast_timer_cb, 1800, NULL);
}

typedef struct {
    void (*fn)(void *user);
    void *user;
} confirm_ctx_t;

static confirm_ctx_t s_confirm_ctx;

static void modal_close(void)
{
    if (s_modal) {
        lv_obj_delete(s_modal);
        s_modal = NULL;
    }
}

static void modal_cancel_cb(lv_event_t *e)
{
    (void)e;
    modal_close();
}

static void modal_ok_cb(lv_event_t *e)
{
    (void)e;
    confirm_ctx_t ctx = s_confirm_ctx;
    modal_close();
    if (ctx.fn) {
        ctx.fn(ctx.user);
    }
}

void ui_confirm(const char *message, const char *ok_text, void (*on_ok)(void *user), void *user)
{
    const ui_theme_t *t = ui_theme();
    modal_close();

    lv_obj_t *mask = lv_obj_create(lv_layer_top());
    lv_obj_set_size(mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_40, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *box = lv_obj_create(mask);
    lv_obj_set_size(box, 380, 220);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, t->panel, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = ui_label(box, message, fonts_ui(), t->fg);
    lv_obj_set_width(label, 320);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *cancel = ui_button(box, "取消", 130, 56, modal_cancel_cb, NULL);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 24, -24);

    lv_obj_t *ok = ui_button(box, ok_text ? ok_text : "确定", 130, 56, modal_ok_cb, NULL);
    lv_obj_set_style_bg_color(ok, t->accent, 0);
    lv_obj_t *ok_label = lv_obj_get_child(ok, 0);
    if (ok_label) {
        lv_obj_set_style_text_color(ok_label, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, -24, -24);
    lv_obj_add_event_cb(ok, modal_ok_cb, LV_EVENT_CLICKED, NULL);

    s_confirm_ctx.fn = on_ok;
    s_confirm_ctx.user = user;
    s_modal = mask;
}
