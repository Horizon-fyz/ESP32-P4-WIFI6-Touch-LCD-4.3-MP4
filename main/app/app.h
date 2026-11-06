#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 NVS / SD / WiFi / 字体，并显示书架。需在 bsp_display_start 之后调用。 */
void app_start(void);

#ifdef __cplusplus
}
#endif
