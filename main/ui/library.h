#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ui_library_show(void);
void ui_library_refresh(void);

/* 主题切换后销毁缓存的书架，下次进入时按新配色重建 */
void ui_library_reset(void);

#ifdef __cplusplus
}
#endif
