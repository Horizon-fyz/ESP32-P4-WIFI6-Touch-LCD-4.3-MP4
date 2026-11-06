#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t *starts; /* 每行在全文中的起始偏移 */
    uint16_t *lens;   /* 每行字节数 */
    int count;
    int cap;
    int lines_per_page;
    uint32_t start_offset;
    uint32_t end_offset;
} pagination_t;

/* 对 [start, end) 区间按字体和宽度断行，并按高度分页。 */
bool pagination_build(pagination_t *pg, const char *text, uint32_t start, uint32_t end,
                      const lv_font_t *font, int max_width, int max_height, int line_space);

void pagination_free(pagination_t *pg);

static inline int pagination_page_count(const pagination_t *pg)
{
    if (!pg || pg->lines_per_page <= 0) {
        return 0;
    }
    return (pg->count + pg->lines_per_page - 1) / pg->lines_per_page;
}

/* 取第 page 页文本（malloc，需 free）。 */
char *pagination_page_text(const pagination_t *pg, const char *text, int page);

int pagination_page_of_offset(const pagination_t *pg, uint32_t offset);
uint32_t pagination_page_offset(const pagination_t *pg, int page);

#ifdef __cplusplus
}
#endif
