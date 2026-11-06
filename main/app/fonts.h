#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FONTS_UI_INDEX 0

/* 从 /spiffs 分区加载中文字体（烧录时由 CMake 生成的 SPIFFS 镜像提供）。 */
esp_err_t fonts_init(void);

const lv_font_t *fonts_ui(void);

int fonts_count(void);
int fonts_size_px(int index);

/* 直接获取某个字号（不改变当前选择） */
const lv_font_t *fonts_get(int index);

/* 加载指定下标的阅读字体（切字号时调用），失败返回 NULL。 */
const lv_font_t *fonts_select_reader(int index);
const lv_font_t *fonts_current_reader(void);

#ifdef __cplusplus
}
#endif
