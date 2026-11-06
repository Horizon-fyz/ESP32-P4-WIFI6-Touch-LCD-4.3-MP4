#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 调试用：把当前面板帧缓冲以 RLE + Base64 通过串口输出，
 * 配合 tools/capture_screen.py 可在电脑上还原屏幕截图。 */
void screenshot_dump_serial(void);

/* 同步截取当前画面（调试命令用，需在非 LVGL 任务中调用） */
void screenshot_dump_now(void);

#ifdef __cplusplus
}
#endif
