#include "book.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "book";

static bool has_suffix(const char *s, const char *suffix)
{
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

const char *book_name_from_path(const char *path, char *buf, size_t buf_len)
{
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    size_t n = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        n = (size_t)(dot - base);
    }
    if (n >= buf_len) {
        n = buf_len - 1;
    }
    memcpy(buf, base, n);
    buf[n] = '\0';
    return buf;
}

esp_err_t book_open(const char *path, book_t *book)
{
    memset(book, 0, sizeof(*book));
    strncpy(book->path, path, sizeof(book->path) - 1);
    book_name_from_path(path, book->title, sizeof(book->title));

    if (has_suffix(path, ".epub")) {
        book->type = BOOK_TYPE_EPUB;
        esp_err_t err = book_epub_load(book);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "打开 EPUB 失败: %s", path);
            book_close(book);
        }
        return err;
    }

    if (has_suffix(path, ".txt") || has_suffix(path, ".md") || has_suffix(path, ".text")) {
        book->type = BOOK_TYPE_TXT;
        esp_err_t err = book_txt_load(book);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "打开 TXT 失败: %s", path);
            book_close(book);
        }
        return err;
    }

    ESP_LOGE(TAG, "不支持的格式: %s", path);
    return ESP_ERR_NOT_SUPPORTED;
}

void book_close(book_t *book)
{
    if (!book) {
        return;
    }
    free(book->text);
    free(book->chapters);
    book->text = NULL;
    book->chapters = NULL;
    book->length = 0;
    book->chapter_count = 0;
}

int book_chapter_at(const book_t *book, uint32_t offset)
{
    if (!book || book->chapter_count <= 0) {
        return 0;
    }
    int lo = 0;
    int hi = book->chapter_count - 1;
    int ans = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (book->chapters[mid].offset <= offset) {
            ans = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return ans;
}
