#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOK_TYPE_TXT = 0,
    BOOK_TYPE_EPUB = 1,
} book_type_t;

typedef struct {
    uint32_t offset; /* 章节在全文中的字节偏移 */
    uint32_t length; /* 章节字节长度 */
    char title[64];
} book_chapter_t;

typedef struct {
    char path[192];
    char title[96];
    char author[64];
    book_type_t type;

    char *text;      /* UTF-8 全文，NUL 结尾 */
    size_t length;
    book_chapter_t *chapters;
    int chapter_count;
} book_t;

/* 打开 TXT / EPUB，成功时 book 内字段需由 book_close 释放。 */
esp_err_t book_open(const char *path, book_t *book);
void book_close(book_t *book);

/* 返回 offset 所在章节下标。 */
int book_chapter_at(const book_t *book, uint32_t offset);

/* 从路径推导书名（去掉目录和扩展名），返回 buf。 */
const char *book_name_from_path(const char *path, char *buf, size_t buf_len);

/* 由各格式实现调用 */
esp_err_t book_txt_load(book_t *book);
esp_err_t book_epub_load(book_t *book);

#ifdef __cplusplus
}
#endif
