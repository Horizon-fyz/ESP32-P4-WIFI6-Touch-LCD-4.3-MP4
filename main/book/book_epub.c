#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "book.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "text_util.h"
#include "zip.h"

static const char *TAG = "book_epub";

#define MAX_MANIFEST 512
#define MAX_SPINE 512
#define MAX_TOC 256

typedef struct {
    char id[64];
    char href[192];
    char media[64];
    char properties[64];
} manifest_item_t;

typedef struct {
    char idref[64];
} spine_item_t;

typedef struct {
    char href[192];
    char title[96];
} toc_item_t;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} textbuf_t;

/* -------------------------------------------------------------------------- */

static bool tb_reserve(textbuf_t *tb, size_t extra)
{
    if (tb->len + extra + 1 <= tb->cap) {
        return true;
    }
    size_t cap = tb->cap ? tb->cap : 16384;
    while (cap < tb->len + extra + 1) {
        cap *= 2;
    }
    char *nb = heap_caps_realloc(tb->buf, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nb) {
        nb = realloc(tb->buf, cap);
    }
    if (!nb) {
        return false;
    }
    tb->buf = nb;
    tb->cap = cap;
    return true;
}

static bool tb_append(textbuf_t *tb, const char *s, size_t n)
{
    if (!tb_reserve(tb, n)) {
        return false;
    }
    memcpy(tb->buf + tb->len, s, n);
    tb->len += n;
    tb->buf[tb->len] = '\0';
    return true;
}

/* -------------------------------------------------------------------------- */

static bool attr_value(const char *s, const char *end, const char *name, char *out, size_t out_sz)
{
    size_t n = strlen(name);
    for (const char *p = s; p + n <= end; p++) {
        if (strncmp(p, name, n) != 0) {
            continue;
        }
        if (p != s && !isspace((unsigned char)p[-1]) && p[-1] != '<') {
            continue;
        }
        const char *q = p + n;
        while (q < end && isspace((unsigned char)*q)) {
            q++;
        }
        if (q >= end || *q != '=') {
            continue;
        }
        q++;
        while (q < end && isspace((unsigned char)*q)) {
            q++;
        }
        if (q >= end || (*q != '"' && *q != '\'')) {
            continue;
        }
        char quote = *q++;
        const char *r = q;
        while (r < end && *r != quote) {
            r++;
        }
        size_t len = (size_t)(r - q);
        if (len >= out_sz) {
            len = out_sz - 1;
        }
        memcpy(out, q, len);
        out[len] = '\0';
        return true;
    }
    return false;
}

/* 只解码常用的 XML 实体 */
static void unescape_inplace(char *s)
{
    char *w = s;
    for (char *r = s; *r;) {
        if (*r == '&') {
            struct {
                const char *name;
                char ch;
            } ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            bool done = false;
            for (size_t i = 0; i < sizeof(ents) / sizeof(ents[0]); i++) {
                size_t n = strlen(ents[i].name);
                if (strncmp(r, ents[i].name, n) == 0) {
                    *w++ = ents[i].ch;
                    r += n;
                    done = true;
                    break;
                }
            }
            if (done) {
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

/* 取出标签之间的文本并转成纯文本（用于 dc:title 等） */
static void element_text(const char *doc, const char *open_tag, char *out, size_t out_sz)
{
    out[0] = '\0';
    const char *p = strstr(doc, open_tag);
    if (!p) {
        return;
    }
    const char *start = strchr(p, '>');
    if (!start) {
        return;
    }
    start++;
    char close_tag[64];
    const char *name = open_tag + 1;
    size_t name_len = strcspn(name, " >");
    snprintf(close_tag, sizeof(close_tag), "</%.*s>", (int)name_len, name);
    const char *end = strstr(start, close_tag);
    if (!end) {
        return;
    }
    size_t len = (size_t)(end - start);
    char *tmp = malloc(len + 1);
    if (!tmp) {
        return;
    }
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    size_t tlen = 0;
    char *txt = html_to_text(tmp, len, &tlen);
    free(tmp);
    if (!txt) {
        return;
    }
    size_t n = tlen < out_sz - 1 ? tlen : out_sz - 1;
    memcpy(out, txt, n);
    out[n] = '\0';
    free(txt);
}

static void path_dirname(const char *path, char *out, size_t out_sz)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        out[0] = '\0';
        return;
    }
    size_t n = (size_t)(slash - path) + 1;
    if (n >= out_sz) {
        n = out_sz - 1;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

static void url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r;) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/* 用 base 目录拼接相对路径，并处理 . / .. */
static void path_resolve(const char *base_dir, const char *href, char *out, size_t out_sz)
{
    char joined[384];
    if (href[0] == '/') {
        snprintf(joined, sizeof(joined), "%s", href + 1);
    } else {
        snprintf(joined, sizeof(joined), "%s%s", base_dir, href);
    }

    char *segments[64];
    int count = 0;
    char tmp[384];
    snprintf(tmp, sizeof(tmp), "%s", joined);
    char *save = NULL;
    for (char *tok = strtok_r(tmp, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (strcmp(tok, ".") == 0) {
            continue;
        }
        if (strcmp(tok, "..") == 0) {
            if (count > 0) {
                count--;
            }
            continue;
        }
        if (count < 64) {
            segments[count++] = tok;
        }
    }

    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        strncat(out, segments[i], out_sz - strlen(out) - 1);
        if (i + 1 < count) {
            strncat(out, "/", out_sz - strlen(out) - 1);
        }
    }
    url_decode(out);
}

static char *zip_text(zip_t *z, const char *name)
{
    int idx = zip_find(z, name);
    if (idx < 0) {
        return NULL;
    }
    char *data = NULL;
    size_t len = 0;
    if (zip_read_entry(z, idx, &data, &len) != ESP_OK) {
        return NULL;
    }
    return data;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void strip_fragment(const char *src, char *out, size_t out_sz)
{
    size_t n = 0;
    while (src[n] && src[n] != '#' && n < out_sz - 1) {
        out[n] = src[n];
        n++;
    }
    out[n] = '\0';
}

/* -------------------------------------------------------------------------- */

esp_err_t book_epub_load(book_t *book)
{
    zip_t z;
    if (!zip_open(&z, book->path)) {
        return ESP_FAIL;
    }

    char opf_path[192] = {0};
    char *container = zip_text(&z, "META-INF/container.xml");
    if (container) {
        const char *rf = strstr(container, "rootfile");
        if (rf) {
            const char *tag_end = strchr(rf, '>');
            if (tag_end && attr_value(rf, tag_end, "full-path", opf_path, sizeof(opf_path))) {
                url_decode(opf_path);
            }
        }
        free(container);
    }
    if (opf_path[0] == '\0') {
        ESP_LOGE(TAG, "找不到 OPF");
        zip_close(&z);
        return ESP_FAIL;
    }

    char *opf = zip_text(&z, opf_path);
    if (!opf) {
        ESP_LOGE(TAG, "无法读取 %s", opf_path);
        zip_close(&z);
        return ESP_FAIL;
    }

    manifest_item_t *manifest = calloc(MAX_MANIFEST, sizeof(manifest_item_t));
    spine_item_t *spine = calloc(MAX_SPINE, sizeof(spine_item_t));
    toc_item_t *toc = calloc(MAX_TOC, sizeof(toc_item_t));
    if (!manifest || !spine || !toc) {
        free(manifest);
        free(spine);
        free(toc);
        free(opf);
        zip_close(&z);
        return ESP_ERR_NO_MEM;
    }

    int manifest_count = 0;
    int spine_count = 0;
    int toc_count = 0;

    /* metadata */
    element_text(opf, "<dc:title", book->title, sizeof(book->title));
    element_text(opf, "<dc:creator", book->author, sizeof(book->author));

    /* manifest */
    const char *manifest_end = strstr(opf, "</manifest>");
    if (!manifest_end) {
        manifest_end = opf + strlen(opf);
    }
    const char *p = opf;
    while ((p = strstr(p, "<item")) != NULL && p < manifest_end) {
        if (p[5] != ' ' && p[5] != '\t' && p[5] != '\n' && p[5] != '\r' && p[5] != '/') {
            p += 5;
            continue;
        }
        const char *tag_end = strchr(p, '>');
        if (!tag_end || tag_end > manifest_end) {
            break;
        }
        if (manifest_count < MAX_MANIFEST) {
            manifest_item_t *it = &manifest[manifest_count];
            attr_value(p, tag_end, "id", it->id, sizeof(it->id));
            attr_value(p, tag_end, "href", it->href, sizeof(it->href));
            attr_value(p, tag_end, "media-type", it->media, sizeof(it->media));
            attr_value(p, tag_end, "properties", it->properties, sizeof(it->properties));
            if (it->href[0]) {
                manifest_count++;
            }
        }
        p = tag_end + 1;
    }

    /* spine */
    char spine_toc[64] = {0};
    const char *spine_start = strstr(opf, "<spine");
    if (spine_start) {
        const char *spine_tag_end = strchr(spine_start, '>');
        if (spine_tag_end) {
            attr_value(spine_start, spine_tag_end, "toc", spine_toc, sizeof(spine_toc));
            const char *q = spine_tag_end;
            while ((q = strstr(q, "<itemref")) != NULL) {
                const char *e = strchr(q, '>');
                if (!e) {
                    break;
                }
                if (spine_count < MAX_SPINE) {
                    attr_value(q, e, "idref", spine[spine_count].idref, sizeof(spine[spine_count].idref));
                    if (spine[spine_count].idref[0]) {
                        spine_count++;
                    }
                }
                q = e + 1;
            }
        }
    }

    char opf_dir[192];
    path_dirname(opf_path, opf_dir, sizeof(opf_dir));

    /* 找 TOC（NCX 或 EPUB3 nav） */
    char toc_href[192] = {0};
    for (int i = 0; i < manifest_count; i++) {
        if (spine_toc[0] && strcmp(manifest[i].id, spine_toc) == 0) {
            strncpy(toc_href, manifest[i].href, sizeof(toc_href) - 1);
        }
        if (strstr(manifest[i].properties, "nav")) {
            strncpy(toc_href, manifest[i].href, sizeof(toc_href) - 1);
        }
    }

    if (toc_href[0]) {
        char toc_path[256];
        path_resolve(opf_dir, toc_href, toc_path, sizeof(toc_path));
        char *doc = zip_text(&z, toc_path);
        if (doc) {
            const char *q = doc;
            if (strstr(toc_path, ".ncx")) {
                while ((q = strstr(q, "<navPoint")) != NULL && toc_count < MAX_TOC) {
                    const char *seg_end = strstr(q + 1, "<navPoint");
                    if (!seg_end) {
                        seg_end = doc + strlen(doc);
                    }
                    const char *text_open = strstr(q, "<text>");
                    const char *content = strstr(q, "<content");
                    if (text_open && text_open < seg_end && content && content < seg_end) {
                        const char *text_end = strstr(text_open + 6, "</text>");
                        const char *content_tag_end = strchr(content, '>');
                        if (text_end && text_end < seg_end && content_tag_end) {
                            size_t n = (size_t)(text_end - (text_open + 6));
                            if (n > sizeof(toc[toc_count].title) - 1) {
                                n = sizeof(toc[toc_count].title) - 1;
                            }
                            memcpy(toc[toc_count].title, text_open + 6, n);
                            toc[toc_count].title[n] = '\0';
                            unescape_inplace(toc[toc_count].title);
                            attr_value(content, content_tag_end, "src", toc[toc_count].href,
                                       sizeof(toc[toc_count].href));
                            if (toc[toc_count].href[0]) {
                                toc_count++;
                            }
                        }
                    }
                    q += 9;
                }
            } else {
                /* EPUB3 nav.xhtml: <a href="...">标题</a> */
                while ((q = strstr(q, "<a ")) != NULL && toc_count < MAX_TOC) {
                    const char *tag_end = strchr(q, '>');
                    if (!tag_end) {
                        break;
                    }
                    const char *text_end = strstr(tag_end + 1, "</a>");
                    if (!text_end) {
                        break;
                    }
                    if (attr_value(q, tag_end, "href", toc[toc_count].href, sizeof(toc[toc_count].href))) {
                        char *txt = html_to_text(tag_end + 1, (size_t)(text_end - tag_end - 1), NULL);
                        if (txt) {
                            snprintf(toc[toc_count].title, sizeof(toc[toc_count].title), "%s", txt);
                            free(txt);
                            if (toc[toc_count].href[0] && toc[toc_count].title[0]) {
                                toc_count++;
                            }
                        }
                    }
                    q = text_end + 4;
                }
            }
            free(doc);
        }
    }

    /* 拼接正文 */
    textbuf_t tb = {0};
    if (!tb_reserve(&tb, 4096)) {
        free(manifest);
        free(spine);
        free(toc);
        free(opf);
        zip_close(&z);
        return ESP_ERR_NO_MEM;
    }
    tb.buf[0] = '\0';

    int chapter_cap = 0;
    int chapter_index = 0;

    for (int i = 0; i < spine_count; i++) {
        manifest_item_t *it = NULL;
        for (int k = 0; k < manifest_count; k++) {
            if (strcmp(manifest[k].id, spine[i].idref) == 0) {
                it = &manifest[k];
                break;
            }
        }
        if (!it || strcmp(it->media, "application/xhtml+xml") != 0) {
            continue;
        }

        char item_path[256];
        path_resolve(opf_dir, it->href, item_path, sizeof(item_path));
        char *doc = zip_text(&z, item_path);
        if (!doc) {
            ESP_LOGW(TAG, "缺少章节文件 %s", item_path);
            continue;
        }

        size_t text_len = 0;
        char *plain = html_to_text(doc, strlen(doc), &text_len);
        free(doc);
        if (!plain || text_len == 0) {
            free(plain);
            continue;
        }

        /* 找目录标题 */
        char title[96] = {0};
        for (int t = 0; t < toc_count; t++) {
            char toc_plain[192];
            strip_fragment(toc[t].href, toc_plain, sizeof(toc_plain));
            if (strcmp(base_name(toc_plain), base_name(item_path)) == 0) {
                snprintf(title, sizeof(title), "%s", toc[t].title);
                break;
            }
        }
        if (title[0] == '\0') {
            snprintf(title, sizeof(title), "第 %d 节", ++chapter_index);
        }

        uint32_t offset = (uint32_t)tb.len;
        if (!tb_append(&tb, plain, text_len)) {
            free(plain);
            break;
        }
        tb_append(&tb, "\n\n", 2);
        free(plain);

        if (book->chapter_count >= chapter_cap) {
            int nc = chapter_cap ? chapter_cap * 2 : 32;
            book_chapter_t *grown = realloc(book->chapters, (size_t)nc * sizeof(book_chapter_t));
            if (!grown) {
                break;
            }
            book->chapters = grown;
            chapter_cap = nc;
        }
        book_chapter_t *c = &book->chapters[book->chapter_count++];
        c->offset = offset;
        c->length = (uint32_t)tb.len - offset;
        snprintf(c->title, sizeof(c->title), "%s", title);
    }

    zip_close(&z);
    free(manifest);
    free(spine);
    free(toc);
    free(opf);

    if (book->chapter_count == 0 || tb.len == 0) {
        free(tb.buf);
        return ESP_FAIL;
    }

    book->text = tb.buf;
    book->length = tb.len;
    text_normalize(book->text, &book->length);

    if (book->title[0] == '\0') {
        book_name_from_path(book->path, book->title, sizeof(book->title));
    }

    ESP_LOGI(TAG, "%s: %u 字节, %d 章 (%s)", book->path, (unsigned)book->length, book->chapter_count,
             book->title);
    return ESP_OK;
}
