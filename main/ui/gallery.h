#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ui_gallery_show(void);

/* 调试用：直接打开第 index 张图片的全屏查看 */
void ui_gallery_open_index(int index);

#ifdef __cplusplus
}
#endif
