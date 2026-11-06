#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "book/book.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char path[192];
    char title[96];
    uint32_t size;
    uint8_t type;      /* book_type_t */
    uint16_t percent;  /* 0-1000 */
} lib_book_t;

typedef struct {
    char path[192];
    char name[96];
    uint32_t size;
} media_file_t;

/* 挂载 SD 卡并确保 books / pictures / music / videos 目录存在 */
esp_err_t storage_init(void);
bool storage_ready(void);
int storage_scan(lib_book_t *out, int max);
esp_err_t storage_delete(const char *path);
void storage_free_space(uint64_t *free_bytes, uint64_t *total_bytes);

/* 扫描 /sdcard/<subdir> 下指定扩展名的文件（不递归），按名称排序 */
int storage_scan_media(const char *subdir, const char *const *exts, int ext_count,
                       media_file_t *out, int max);

/* ---------------- 阅读设置 / 进度 / 书签（NVS） ---------------- */

typedef struct {
    uint8_t theme;      /* 0 纸白 1 夜间 2 护眼 */
    uint8_t font_idx;   /* 字号下标 */
    uint8_t line_space; /* 行距像素 */
    uint8_t brightness; /* 10-100 */
    char last_book[192];
    uint32_t last_offset;
} reader_cfg_t;

typedef struct {
    uint32_t offset;
    char title[48];
} reader_bookmark_t;

esp_err_t settings_init(void);
reader_cfg_t *settings_get(void);
void settings_save(void);

uint32_t settings_progress_offset(const char *path);
uint16_t settings_progress_percent(const char *path);
void settings_set_progress(const char *path, uint32_t offset, uint16_t percent);

int settings_bookmarks_get(const char *path, reader_bookmark_t *out, int max);
bool settings_bookmark_add(const char *path, uint32_t offset, const char *title);
void settings_bookmarks_clear(const char *path);

#ifdef __cplusplus
}
#endif
