#pragma once

#include <stdint.h>

#include "book/book.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_reader_init(void);

/* 打开书籍并进入阅读界面 */
void ui_reader_open(const char *path);

/* 当前已打开的书（供目录/书签使用），未打开返回 NULL */
book_t *ui_reader_book(void);

/* 跳转到章节 / 全文偏移 */
void ui_reader_goto_chapter(int chapter);
void ui_reader_goto_offset(uint32_t offset);

/* 进度保存 */
void ui_reader_save_progress(void);

/* 主题/字号变化后重绘当前页 */
void ui_reader_reapply(void);

/* 返回阅读界面 / 当前章节下标 */
void ui_reader_show_current(void);
int ui_reader_current_chapter(void);

#ifdef __cplusplus
}
#endif
