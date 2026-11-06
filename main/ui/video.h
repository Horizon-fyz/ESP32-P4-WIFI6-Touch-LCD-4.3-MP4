#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ui_video_show(void);

/* 调试用：直接播放指定文件 */
void ui_video_play_file(const char *path);

#ifdef __cplusplus
}
#endif
