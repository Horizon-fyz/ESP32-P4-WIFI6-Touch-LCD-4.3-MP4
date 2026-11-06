#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_color_t bg;
    lv_color_t panel;
    lv_color_t fg;
    lv_color_t muted;
    lv_color_t accent;
    lv_color_t line;
} ui_theme_t;

#define UI_THEME_COUNT 3

const ui_theme_t *ui_theme(void);
int ui_theme_index(void);
void ui_theme_set_index(int index);
const char *ui_theme_name(int index);

#ifdef __cplusplus
}
#endif
