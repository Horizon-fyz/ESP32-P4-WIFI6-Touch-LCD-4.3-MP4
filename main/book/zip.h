#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIP_MAX_ENTRIES 512
#define ZIP_MAX_NAME 160

typedef struct {
    char name[ZIP_MAX_NAME];
    uint16_t method;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_offset;
} zip_entry_t;

typedef struct {
    FILE *fp;
    zip_entry_t entries[ZIP_MAX_ENTRIES];
    int count;
} zip_t;

/* 打开 zip（EPUB 本质是 zip），读取中央目录。 */
bool zip_open(zip_t *z, const char *path);
void zip_close(zip_t *z);

/* 精确匹配或忽略大小写匹配，返回下标，未找到返回 -1。 */
int zip_find(zip_t *z, const char *name);

/* 解压条目到新分配的缓冲区（NUL 结尾），支持 stored / deflate。 */
esp_err_t zip_read_entry(zip_t *z, int idx, char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif
