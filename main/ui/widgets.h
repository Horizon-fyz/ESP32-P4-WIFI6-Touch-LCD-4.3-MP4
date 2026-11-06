#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_screen_new(void);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb, void *user);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);

/* 底部浮层提示，1.8 秒后自动消失 */
void ui_toast(const char *fmt, ...);

/* 简易确认框，确认后调用 on_ok(user) */
void ui_confirm(const char *message, const char *ok_text, void (*on_ok)(void *user), void *user);

#ifdef __cplusplus
}
#endif
