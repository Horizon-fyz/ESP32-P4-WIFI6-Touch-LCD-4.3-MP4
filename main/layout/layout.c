#include "layout.h"

#include <stdlib.h>
#include <string.h>

#include "book/text_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "layout";

static int32_t glyph_width(const lv_font_t *font, uint32_t cp)
{
    return lv_font_get_glyph_width(font, cp, 0);
}

static bool add_line(pagination_t *pg, uint32_t start, uint32_t len)
{
    if (pg->count >= pg->cap) {
        int nc = pg->cap ? pg->cap * 2 : 256;
        uint32_t *ns = realloc(pg->starts, (size_t)nc * sizeof(uint32_t));
        uint16_t *nl = realloc(pg->lens, (size_t)nc * sizeof(uint16_t));
        if (!ns || !nl) {
            free(ns ? ns : pg->starts);
            free(nl ? nl : pg->lens);
            pg->starts = NULL;
            pg->lens = NULL;
            pg->count = 0;
            pg->cap = 0;
            return false;
        }
        pg->starts = ns;
        pg->lens = nl;
        pg->cap = nc;
    }
    pg->starts[pg->count] = start;
    pg->lens[pg->count] = (uint16_t)(len > 0xFFFF ? 0xFFFF : len);
    pg->count++;
    return true;
}

bool pagination_build(pagination_t *pg, const char *text, uint32_t start, uint32_t end,
                      const lv_font_t *font, int max_width, int max_height, int line_space)
{
    memset(pg, 0, sizeof(*pg));
    pg->start_offset = start;
    pg->end_offset = end;

    if (!text || end <= start || max_width <= 0 || max_height <= 0 || !font) {
        return false;
    }

    int line_height = font->line_height + line_space;
    if (line_height <= 0) {
        line_height = font->line_height;
    }
    pg->lines_per_page = (max_height + line_space) / line_height;
    if (pg->lines_per_page < 1) {
        pg->lines_per_page = 1;
    }

    uint32_t pos = start;
    while (pos < end && (text[pos] == '\n' || text[pos] == '\r')) {
        pos++;
    }

    while (pos < end) {
        uint32_t line_start = pos;
        uint32_t p = pos;
        int32_t width = 0;
        uint32_t last_break = 0;

        while (p < end) {
            uint32_t cp;
            int n = utf8_next(text + p, &cp);
            if (cp == '\n') {
                break;
            }
            if (cp == '\r') {
                p += (uint32_t)n;
                continue;
            }

            int32_t adv = glyph_width(font, cp);
            if (width + adv > max_width && p > line_start) {
                if (last_break > line_start) {
                    p = last_break;
                }
                break;
            }

            width += adv;
            p += (uint32_t)n;

            if (cp == ' ') {
                last_break = p;
            } else if (cp_is_cjk(cp)) {
                if (!cp_cannot_end_line(cp)) {
                    last_break = p;
                }
                if (cp_cannot_start_line(cp)) {
                    last_break = p;
                }
            }
        }

        uint32_t line_end = p;
        if (line_end > end) {
            line_end = end;
        }
        while (line_end > line_start && (text[line_end - 1] == ' ' || text[line_end - 1] == '\t')) {
            line_end--;
        }

        if (line_end > line_start) {
            if (!add_line(pg, line_start, line_end - line_start)) {
                ESP_LOGE(TAG, "内存不足，分页中断");
                return false;
            }
        }

        if (p < end && text[p] == '\n') {
            pos = p + 1;
        } else {
            pos = p;
        }

        /* 下一行跳过行首空白 */
        while (pos < end && (text[pos] == ' ' || text[pos] == '\t')) {
            pos++;
        }
    }

    if (pg->count == 0) {
        return false;
    }

    ESP_LOGI(TAG, "分页: %d 行, 每页 %d 行, 共 %d 页", pg->count, pg->lines_per_page,
             pagination_page_count(pg));
    return true;
}

void pagination_free(pagination_t *pg)
{
    if (!pg) {
        return;
    }
    free(pg->starts);
    free(pg->lens);
    memset(pg, 0, sizeof(*pg));
}

char *pagination_page_text(const pagination_t *pg, const char *text, int page)
{
    if (!pg || pg->count == 0 || page < 0) {
        return NULL;
    }
    int first = page * pg->lines_per_page;
    if (first >= pg->count) {
        return NULL;
    }
    int last = first + pg->lines_per_page;
    if (last > pg->count) {
        last = pg->count;
    }

    size_t total = 0;
    for (int i = first; i < last; i++) {
        total += pg->lens[i] + 1;
    }

    char *buf = heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = malloc(total + 1);
    }
    if (!buf) {
        return NULL;
    }

    size_t o = 0;
    for (int i = first; i < last; i++) {
        memcpy(buf + o, text + pg->starts[i], pg->lens[i]);
        o += pg->lens[i];
        if (i + 1 < last) {
            buf[o++] = '\n';
        }
    }
    buf[o] = '\0';
    return buf;
}

int pagination_page_of_offset(const pagination_t *pg, uint32_t offset)
{
    if (!pg || pg->count == 0) {
        return 0;
    }
    int lo = 0;
    int hi = pg->count - 1;
    int ans = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (pg->starts[mid] <= offset) {
            ans = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return ans / pg->lines_per_page;
}

uint32_t pagination_page_offset(const pagination_t *pg, int page)
{
    if (!pg || pg->count == 0) {
        return 0;
    }
    int line = page * pg->lines_per_page;
    if (line >= pg->count) {
        line = pg->count - 1;
    }
    return pg->starts[line];
}
