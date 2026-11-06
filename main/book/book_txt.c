#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "book.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "text_util.h"

static const char *TAG = "book_txt";

#define MAX_TXT_BYTES (32u * 1024 * 1024)
#define FALLBACK_CHUNK 12000

static bool starts_with(const char *s, size_t len, const char *prefix)
{
    size_t n = strlen(prefix);
    return len >= n && memcmp(s, prefix, n) == 0;
}

static bool ends_with_punct(const char *s, size_t len)
{
    static const char *puncts[] = {"。", "！", "？", "，", "；", "：", "、", ".", ",", "!", "?", ";", ":"};
    for (size_t i = 0; i < sizeof(puncts) / sizeof(puncts[0]); i++) {
        size_t n = strlen(puncts[i]);
        if (len >= n && memcmp(s + len - n, puncts[i], n) == 0) {
            return true;
        }
    }
    return false;
}

/* 中文数字、阿拉伯数字、空格 */
static bool is_numeral(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') {
        return true;
    }
    if (cp >= 0xFF10 && cp <= 0xFF19) {
        return true;
    }
    static const char *nums[] = {"零", "〇", "一", "二", "三", "四", "五", "六", "七",
                                 "八", "九", "十", "百", "千", "万", "两", "壹", "贰",
                                 "叁", "肆", "伍", "陆", "柒", "捌", "玖", "拾"};
    for (size_t i = 0; i < sizeof(nums) / sizeof(nums[0]); i++) {
        size_t n = strlen(nums[i]);
        char buf[4] = {0};
        utf8_encode(cp, buf);
        if (memcmp(buf, nums[i], n) == 0) {
            return true;
        }
    }
    return false;
}

/* 判断一行是否像章节标题 */
static bool looks_like_chapter(const char *s, size_t len)
{
    if (len == 0 || len > 96) {
        return false;
    }

    size_t chars = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        i += (size_t)utf8_next(s + i, &cp);
        chars++;
    }
    if (chars > 32) {
        return false;
    }

    if (ends_with_punct(s, len)) {
        return false;
    }

    if (starts_with(s, len, "序章") || starts_with(s, len, "楔子") || starts_with(s, len, "前言") ||
        starts_with(s, len, "引子") || starts_with(s, len, "后记") || starts_with(s, len, "尾声") ||
        starts_with(s, len, "番外") || starts_with(s, len, "附录") ||
        starts_with(s, len, "Chapter") || starts_with(s, len, "CHAPTER") || starts_with(s, len, "chapter")) {
        return true;
    }

    if (!starts_with(s, len, "第")) {
        return false;
    }

    /* 第 [数字] 章 / 节 / 回 / 卷 / 篇 / 集 / 话 / 部 */
    size_t i = strlen("第");
    int numerals = 0;
    while (i < len && numerals < 8) {
        uint32_t cp;
        int adv = utf8_next(s + i, &cp);
        if (is_numeral(cp) || cp == ' ' || cp == 0x3000) {
            i += (size_t)adv;
            numerals++;
            continue;
        }
        break;
    }
    if (numerals == 0) {
        return false;
    }

    static const char *markers[] = {"章", "节", "回", "卷", "篇", "集", "话", "部"};
    for (size_t k = 0; k < sizeof(markers) / sizeof(markers[0]); k++) {
        size_t n = strlen(markers[k]);
        if (i + n <= len && memcmp(s + i, markers[k], n) == 0) {
            return true;
        }
    }
    return false;
}

static void add_chapter(book_t *book, int *cap, uint32_t off, uint32_t len, const char *title, size_t title_len)
{
    if (book->chapter_count >= *cap) {
        int nc = *cap ? *cap * 2 : 128;
        book_chapter_t *p = realloc(book->chapters, (size_t)nc * sizeof(book_chapter_t));
        if (!p) {
            return;
        }
        book->chapters = p;
        *cap = nc;
    }
    book_chapter_t *c = &book->chapters[book->chapter_count++];
    c->offset = off;
    c->length = len;

    size_t n = title_len < sizeof(c->title) - 1 ? title_len : sizeof(c->title) - 1;
    /* 避免截断在多字节字符中间 */
    while (n > 0 && ((unsigned char)title[n] & 0xC0) == 0x80) {
        n--;
    }
    memcpy(c->title, title, n);
    c->title[n] = '\0';
    if (n == 0) {
        snprintf(c->title, sizeof(c->title), "第 %d 节", book->chapter_count);
    }
}

static void build_fallback(book_t *book)
{
    int cap = 0;
    uint32_t start = 0;
    int index = 1;
    char title[48];

    while (start < book->length) {
        uint32_t end = start + FALLBACK_CHUNK;
        if (end >= book->length) {
            end = (uint32_t)book->length;
        } else {
            /* 回退到换行处，避免切断段落 */
            uint32_t probe = end;
            while (probe > start + FALLBACK_CHUNK / 2 && book->text[probe] != '\n') {
                probe--;
            }
            if (book->text[probe] == '\n') {
                end = probe + 1;
            }
        }
        snprintf(title, sizeof(title), "第 %d 节", index++);
        add_chapter(book, &cap, start, end - start, title, strlen(title));
        start = end;
    }
}

esp_err_t book_txt_load(book_t *book)
{
    FILE *f = fopen(book->path, "rb");
    if (!f) {
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size <= 0 || size > (long)MAX_TXT_BYTES) {
        ESP_LOGE(TAG, "文件大小异常: %ld", size);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);

    uint8_t *raw = heap_caps_malloc((size_t)size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw) {
        raw = malloc((size_t)size + 1);
    }
    if (!raw) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t got = fread(raw, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(raw);
        return ESP_FAIL;
    }
    raw[size] = '\0';

    size_t text_len = 0;
    char *text = text_to_utf8(raw, (size_t)size, &text_len);
    free(raw);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    text_normalize(text, &text_len);

    book->text = text;
    book->length = text_len;

    /* 收集章节起始位置 */
    uint32_t *offs = NULL;
    char (*titles)[64] = NULL;
    int count = 0;
    int cap = 0;

    for (size_t pos = 0; pos < text_len;) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < text_len && text[line_end] != '\n') {
            line_end++;
        }
        size_t tlen = 0;
        char *trimmed = text_trim(text + line_start, line_end - line_start, &tlen);
        if (looks_like_chapter(trimmed, tlen)) {
            if (count >= cap) {
                int nc = cap ? cap * 2 : 256;
                uint32_t *no = realloc(offs, (size_t)nc * sizeof(uint32_t));
                char(*nt)[64] = realloc(titles, (size_t)nc * 64);
                if (no) {
                    offs = no;
                }
                if (nt) {
                    titles = nt;
                }
                if (!no || !nt) {
                    break;
                }
                cap = nc;
            }
            offs[count] = (uint32_t)(trimmed - text);
            size_t n = tlen < 63 ? tlen : 63;
            while (n > 0 && ((unsigned char)trimmed[n] & 0xC0) == 0x80) {
                n--;
            }
            memcpy(titles[count], trimmed, n);
            titles[count][n] = '\0';
            count++;
        }
        pos = line_end < text_len ? line_end + 1 : text_len;
    }

    if (count >= 2) {
        int ch_cap = 0;
        if (offs[0] > 0) {
            size_t lead = 0;
            for (size_t i = 0; i < offs[0]; i++) {
                if (!isspace((unsigned char)text[i])) {
                    lead++;
                }
            }
            if (lead >= 20) {
                add_chapter(book, &ch_cap, 0, offs[0], "开篇", strlen("开篇"));
            }
        }
        for (int i = 0; i < count; i++) {
            uint32_t end = (i + 1 < count) ? offs[i + 1] : (uint32_t)text_len;
            add_chapter(book, &ch_cap, offs[i], end - offs[i], titles[i], strlen(titles[i]));
        }
    } else {
        build_fallback(book);
    }

    free(offs);
    free(titles);

    if (book->chapter_count == 0) {
        build_fallback(book);
    }
    if (book->chapter_count == 0) {
        return ESP_FAIL;
    }

    /* 书名：优先使用第一行内容 */
    size_t first_end = 0;
    while (first_end < text_len && text[first_end] != '\n') {
        first_end++;
    }
    if (first_end > 0 && first_end < 60) {
        size_t tlen = 0;
        char *trimmed = text_trim(text, first_end, &tlen);
        if (tlen > 0 && tlen < sizeof(book->title) - 1) {
            memcpy(book->title, trimmed, tlen);
            book->title[tlen] = '\0';
        }
    }

    ESP_LOGI(TAG, "%s: %u 字节, %d 章", book->path, (unsigned)book->length, book->chapter_count);
    return ESP_OK;
}
