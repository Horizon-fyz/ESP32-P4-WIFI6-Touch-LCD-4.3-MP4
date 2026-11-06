#include "text_util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "gbk_table.h"

static void *ps_malloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static void *ps_realloc(void *p, size_t n)
{
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
}

int utf8_next(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t b = p[0];

    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if ((b & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
        return (*cp >= 0x80) ? 2 : 1;
    }
    if ((b & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return (*cp >= 0x800 && !(*cp >= 0xD800 && *cp <= 0xDFFF)) ? 3 : 1;
    }
    if ((b & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
              ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return (*cp >= 0x10000 && *cp <= 0x10FFFF) ? 4 : 1;
    }

    *cp = '?';
    return 1;
}

int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

bool utf8_is_valid(const uint8_t *data, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t b = data[i];
        if (b < 0x80) {
            i++;
            continue;
        }
        int need;
        uint32_t cp;
        if ((b & 0xE0) == 0xC0) {
            need = 1;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            need = 2;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            need = 3;
            cp = b & 0x07;
        } else {
            return false;
        }
        if (i + need >= len) {
            return false;
        }
        for (int k = 1; k <= need; k++) {
            if ((data[i + k] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (data[i + k] & 0x3F);
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += need + 1;
    }
    return true;
}

bool cp_is_cjk(uint32_t cp)
{
    if (cp >= 0x2E80 && cp <= 0x9FFF) {
        return true;
    }
    if (cp >= 0xF900 && cp <= 0xFAFF) {
        return true;
    }
    if (cp >= 0xFF00 && cp <= 0xFFEF) {
        return true;
    }
    if (cp >= 0x3000 && cp <= 0x303F) {
        return true;
    }
    return false;
}

bool cp_cannot_start_line(uint32_t cp)
{
    switch (cp) {
    case 0x3001: /* 、 */
    case 0x3002: /* 。 */
    case 0x3009:
    case 0x300B:
    case 0x300D:
    case 0x300F:
    case 0x3011:
    case 0x3015:
    case 0x3017:
    case 0x3019:
    case 0x301B:
    case 0x301E:
    case 0x301F:
    case 0xFF01: /* ！ */
    case 0xFF09: /* ） */
    case 0xFF0C: /* ， */
    case 0xFF0E: /* ． */
    case 0xFF1A: /* ： */
    case 0xFF1B: /* ； */
    case 0xFF1F: /* ？ */
    case 0xFF3D:
    case 0xFF5D:
    case 0x2026: /* … */
    case 0x2014: /* — */
    case 0x201D: /* ” */
    case 0x2019: /* ’ */
    case '.':
    case ',':
    case '!':
    case '?':
    case ':':
    case ';':
    case ')':
    case ']':
    case '}':
    case '%':
        return true;
    default:
        return false;
    }
}

bool cp_cannot_end_line(uint32_t cp)
{
    switch (cp) {
    case 0x3008:
    case 0x300A:
    case 0x300C:
    case 0x300E:
    case 0x3010:
    case 0x3014:
    case 0x3016:
    case 0x3018:
    case 0x301A:
    case 0x301D:
    case 0xFF08: /* （ */
    case 0xFF3B:
    case 0xFF5B:
    case 0x201C: /* “ */
    case 0x2018: /* ‘ */
    case '(':
    case '[':
    case '{':
        return true;
    default:
        return false;
    }
}

/* -------------------------------------------------------------------------- */
/*                             编码识别与转换                                  */
/* -------------------------------------------------------------------------- */

static size_t utf16_to_utf8(const uint8_t *data, size_t len, bool big_endian, char **out)
{
    size_t cap = len * 2 + 8;
    char *buf = ps_malloc(cap);
    if (!buf) {
        return 0;
    }
    size_t o = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint32_t u = big_endian ? ((data[i] << 8) | data[i + 1]) : (data[i] | (data[i + 1] << 8));
        if (u >= 0xD800 && u <= 0xDBFF && i + 3 < len) {
            uint32_t lo = big_endian ? ((data[i + 2] << 8) | data[i + 3])
                                     : (data[i + 2] | (data[i + 3] << 8));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (o + 4 >= cap) {
            cap *= 2;
            char *nb = ps_realloc(buf, cap);
            if (!nb) {
                free(buf);
                return 0;
            }
            buf = nb;
        }
        o += utf8_encode(u, buf + o);
    }
    buf[o] = '\0';
    *out = buf;
    return o;
}

static size_t gbk_to_utf8(const uint8_t *data, size_t len, char **out)
{
    size_t cap = len * 3 + 8;
    char *buf = ps_malloc(cap);
    if (!buf) {
        return 0;
    }
    size_t o = 0;
    for (size_t i = 0; i < len;) {
        uint8_t b = data[i];
        if (b < 0x80) {
            buf[o++] = (char)b;
            i++;
            continue;
        }
        if (b == 0x80) { /* CP936 的欧元符号 */
            o += utf8_encode(0x20AC, buf + o);
            i++;
            continue;
        }
        if (i + 1 < len) {
            uint8_t t = data[i + 1];
            if (t >= 0x30 && t <= 0x39) { /* GB18030 四字节，表内未收录 */
                buf[o++] = '?';
                i += 4;
                continue;
            }
            uint16_t u = gbk_lookup(b, t);
            if (u) {
                o += utf8_encode(u, buf + o);
            } else {
                buf[o++] = '?';
            }
            i += 2;
            continue;
        }
        buf[o++] = '?';
        i++;
    }
    buf[o] = '\0';
    *out = buf;
    return o;
}

char *text_to_utf8(const uint8_t *data, size_t len, size_t *out_len)
{
    char *buf = NULL;
    size_t out = 0;

    if (!data || len == 0) {
        return NULL;
    }

    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        len -= 3;
    } else if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        out = utf16_to_utf8(data + 2, len - 2, false, &buf);
        if (buf) {
            *out_len = out;
            return buf;
        }
        return NULL;
    } else if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        out = utf16_to_utf8(data + 2, len - 2, true, &buf);
        if (buf) {
            *out_len = out;
            return buf;
        }
        return NULL;
    }

    /* 采样判断是否为合法 UTF-8 */
    size_t sample = len > (2 * 1024 * 1024) ? (2 * 1024 * 1024) : len;
    if (utf8_is_valid(data, sample)) {
        buf = ps_malloc(len + 1);
        if (!buf) {
            return NULL;
        }
        memcpy(buf, data, len);
        buf[len] = '\0';
        *out_len = len;
        return buf;
    }

    out = gbk_to_utf8(data, len, &buf);
    if (!buf) {
        return NULL;
    }
    *out_len = out;
    return buf;
}

/* -------------------------------------------------------------------------- */
/*                              HTML -> 纯文本                                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} strbuf_t;

static bool sb_reserve(strbuf_t *sb, size_t extra)
{
    if (sb->len + extra + 1 <= sb->cap) {
        return true;
    }
    size_t cap = sb->cap ? sb->cap : 4096;
    while (cap < sb->len + extra + 1) {
        cap *= 2;
    }
    char *nb = ps_realloc(sb->buf, cap);
    if (!nb) {
        return false;
    }
    sb->buf = nb;
    sb->cap = cap;
    return true;
}

static void sb_putc(strbuf_t *sb, char c)
{
    if (!sb_reserve(sb, 1)) {
        return;
    }
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static void sb_puts(strbuf_t *sb, const char *s, size_t n)
{
    if (!sb_reserve(sb, n)) {
        return;
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_put_utf8(strbuf_t *sb, uint32_t cp)
{
    char tmp[4];
    int n = utf8_encode(cp, tmp);
    sb_puts(sb, tmp, n);
}

static bool entity_decode(const char *p, size_t remain, uint32_t *cp, int *consumed)
{
    /* p 指向 '&'，remain 为剩余长度 */
    if (remain < 3) {
        return false;
    }
    if (p[1] == '#') {
        uint32_t v = 0;
        size_t i = 2;
        bool hex = false;
        if (i < remain && (p[i] == 'x' || p[i] == 'X')) {
            hex = true;
            i++;
        }
        size_t start = i;
        while (i < remain && p[i] != ';' && i - start < 8) {
            char c = p[i];
            int d;
            if (c >= '0' && c <= '9') {
                d = c - '0';
            } else if (hex && c >= 'a' && c <= 'f') {
                d = c - 'a' + 10;
            } else if (hex && c >= 'A' && c <= 'F') {
                d = c - 'A' + 10;
            } else {
                return false;
            }
            v = v * (hex ? 16 : 10) + d;
            i++;
        }
        if (i >= remain || p[i] != ';') {
            return false;
        }
        *cp = v;
        *consumed = (int)i + 1;
        return true;
    }

    static const struct {
        const char *name;
        uint32_t cp;
    } ents[] = {
        {"lt", '<'},     {"gt", '>'},      {"amp", '&'},    {"quot", '"'},
        {"apos", '\''},  {"nbsp", 0x00A0}, {"mdash", 0x2014}, {"ndash", 0x2013},
        {"hellip", 0x2026}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"lsquo", 0x2018},
        {"rsquo", 0x2019}, {"middot", 0x00B7}, {"copy", 0x00A9}, {"reg", 0x00AE},
    };
    for (size_t k = 0; k < sizeof(ents) / sizeof(ents[0]); k++) {
        size_t n = strlen(ents[k].name);
        if (remain > n + 1 && strncmp(p + 1, ents[k].name, n) == 0 && p[n + 1] == ';') {
            *cp = ents[k].cp;
            *consumed = (int)n + 2;
            return true;
        }
    }
    return false;
}

static bool tag_is_block(const char *name, size_t n)
{
    static const char *blocks[] = {
        "p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6",
        "section", "article", "blockquote", "pre", "hr", "title", "td",
    };
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (strlen(blocks[i]) == n && strncasecmp(name, blocks[i], n) == 0) {
            return true;
        }
    }
    return false;
}

char *html_to_text(const char *html, size_t len, size_t *out_len)
{
    strbuf_t sb = {0};
    if (!sb_reserve(&sb, 256)) {
        return NULL;
    }
    sb.buf[0] = '\0';

    int skip = 0; /* 0 正常，1 script，2 style，3 head */
    bool pending_space = false;

    for (size_t i = 0; i < len;) {
        char c = html[i];

        if (c == '<') {
            if (len - i >= 4 && strncmp(html + i, "<!--", 4) == 0) {
                const char *end = strstr(html + i + 4, "-->");
                i = end ? (size_t)(end - html) + 3 : len;
                continue;
            }
            if (len - i >= 2 && (html[i + 1] == '!' || html[i + 1] == '?')) {
                const char *end = memchr(html + i, '>', len - i);
                i = end ? (size_t)(end - html) + 1 : len;
                continue;
            }

            const char *end = memchr(html + i, '>', len - i);
            if (!end) {
                break;
            }
            size_t tag_len = (size_t)(end - html) - i - 1;
            const char *tag = html + i + 1;
            bool closing = false;
            if (tag_len > 0 && tag[0] == '/') {
                closing = true;
                tag++;
                tag_len--;
            }
            size_t n = 0;
            while (n < tag_len && (isalnum((unsigned char)tag[n]) || tag[n] == '-')) {
                n++;
            }

            if (n > 0) {
                if (!closing) {
                    if (n == 6 && strncasecmp(tag, "script", 6) == 0) {
                        skip = 1;
                    } else if (n == 5 && strncasecmp(tag, "style", 5) == 0) {
                        skip = 2;
                    } else if (n == 4 && strncasecmp(tag, "head", 4) == 0) {
                        skip = 3;
                    } else if (skip == 0 && tag_is_block(tag, n)) {
                        if (sb.len > 0 && sb.buf[sb.len - 1] != '\n') {
                            sb_putc(&sb, '\n');
                        }
                    }
                } else {
                    if ((skip == 1 && n == 6 && strncasecmp(tag, "script", 6) == 0) ||
                        (skip == 2 && n == 5 && strncasecmp(tag, "style", 5) == 0) ||
                        (skip == 3 && n == 4 && strncasecmp(tag, "head", 4) == 0)) {
                        skip = 0;
                    } else if (skip == 0 && tag_is_block(tag, n)) {
                        if (sb.len > 0 && sb.buf[sb.len - 1] != '\n') {
                            sb_putc(&sb, '\n');
                        }
                    }
                }
            }
            i = (size_t)(end - html) + 1;
            continue;
        }

        if (skip != 0) {
            i++;
            continue;
        }

        if (c == '&') {
            uint32_t cp;
            int consumed;
            if (entity_decode(html + i, len - i, &cp, &consumed)) {
                if (cp == 0x00A0) {
                    pending_space = true;
                } else if (cp == '\n' || cp == '\r') {
                    if (sb.len == 0 || sb.buf[sb.len - 1] != '\n') {
                        sb_putc(&sb, '\n');
                    }
                } else {
                    if (pending_space && sb.len > 0 && sb.buf[sb.len - 1] != '\n') {
                        sb_putc(&sb, ' ');
                    }
                    pending_space = false;
                    sb_put_utf8(&sb, cp);
                }
                i += consumed;
                continue;
            }
        }

        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            pending_space = true;
            i++;
            continue;
        }

        if (pending_space) {
            if (sb.len > 0 && sb.buf[sb.len - 1] != '\n') {
                sb_putc(&sb, ' ');
            }
            pending_space = false;
        }
        sb_putc(&sb, c);
        i++;
    }

    if (!sb.buf) {
        return NULL;
    }

    /* 连续空行压缩为最多一行 */
    size_t w = 0;
    int nl = 0;
    for (size_t r = 0; r < sb.len; r++) {
        char ch = sb.buf[r];
        if (ch == '\n') {
            nl++;
            if (nl > 1) {
                continue;
            }
        } else if (ch != ' ' && ch != '\t') {
            nl = 0;
        }
        sb.buf[w++] = ch;
    }
    sb.buf[w] = '\0';
    sb.len = w;

    if (out_len) {
        *out_len = sb.len;
    }
    return sb.buf;
}

void text_normalize(char *text, size_t *len)
{
    if (!text || !len) {
        return;
    }
    size_t n = *len;
    size_t w = 0;
    for (size_t r = 0; r < n; r++) {
        char c = text[r];
        if (c == '\r') {
            text[w++] = '\n';
            if (r + 1 < n && text[r + 1] == '\n') {
                r++;
            }
        } else if ((unsigned char)c == 0xEF && r + 2 < n &&
                   (unsigned char)text[r + 1] == 0xBB && (unsigned char)text[r + 2] == 0xBF) {
            r += 2;
        } else {
            text[w++] = c;
        }
    }
    text[w] = '\0';
    *len = w;
}

char *text_trim(char *s, size_t len, size_t *out_len)
{
    size_t start = 0;
    size_t end = len;
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) {
        start++;
    }
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' ||
                           s[end - 1] == '\r')) {
        end--;
    }
    if (out_len) {
        *out_len = end - start;
    }
    return s + start;
}
