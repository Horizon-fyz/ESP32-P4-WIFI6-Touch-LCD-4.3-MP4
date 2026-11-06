#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 桌面（首页）：状态栏 + 应用宫格 */
void ui_home_show(void);

/* 回到首页时刷新状态栏 / 存储信息 */
void ui_home_refresh(void);

/* 主题切换后销毁缓存的首页，下次进入时按新配色重建 */
void ui_home_reset(void);

#ifdef __cplusplus
}
#endif
