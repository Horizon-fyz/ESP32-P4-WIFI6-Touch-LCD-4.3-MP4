#include "web_server.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "storage/storage.h"
#include "ui/library.h"
#include "wifi.h"

static const char *TAG = "webserver";

#define MAX_UPLOAD_BYTES (16 * 1024 * 1024)
#define BOOKS_JSON_MAX 8192

static httpd_handle_t s_server;

/* -------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} hbuf_t;

static void hbuf_init(hbuf_t *b, size_t cap)
{
    b->buf = malloc(cap);
    b->len = 0;
    b->cap = cap;
    if (b->buf) {
        b->buf[0] = '\0';
    }
}

static void hbuf_printf(hbuf_t *b, const char *fmt, ...)
{
    if (!b->buf || b->len >= b->cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        b->len += (size_t)n;
        if (b->len >= b->cap) {
            b->len = b->cap - 1;
        }
    }
}

static void html_escape(hbuf_t *b, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '<':
            hbuf_printf(b, "&lt;");
            break;
        case '>':
            hbuf_printf(b, "&gt;");
            break;
        case '&':
            hbuf_printf(b, "&amp;");
            break;
        case '"':
            hbuf_printf(b, "&quot;");
            break;
        default:
            hbuf_printf(b, "%c", *s);
            break;
        }
    }
}

static void url_decode_inplace(char *s)
{
    char *w = s;
    for (char *r = s; *r;) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        } else if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static char *read_body(httpd_req_t *req, size_t max, size_t *out_len)
{
    if (req->content_len <= 0 || (size_t)req->content_len > max) {
        return NULL;
    }
    size_t total = (size_t)req->content_len;
    char *buf = malloc(total + 1);
    if (!buf) {
        return NULL;
    }
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        got += (size_t)r;
    }
    buf[total] = '\0';
    *out_len = total;
    return buf;
}

static void refresh_library_ui(void)
{
    bsp_display_lock(0);
    ui_library_refresh();
    bsp_display_unlock();
}

/* -------------------------------------------------------------------------- */

static const char *PAGE_HEAD =
    "<!DOCTYPE html><html lang=zh><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>MP4 阅读器</title><style>"
    "body{font-family:system-ui,-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;"
    "background:#f6f1e7;color:#2b2b2b;margin:0;padding:24px;max-width:760px;margin:0 auto}"
    "h1{font-size:24px;margin:0 0 4px}h2{font-size:17px;margin:28px 0 10px;color:#8b5e3c}"
    ".muted{color:#8a8378;font-size:13px}"
    ".card{background:#fff;border-radius:14px;padding:16px;margin-top:12px}"
    ".row{display:flex;align-items:center;justify-content:space-between;gap:10px;"
    "padding:10px 0;border-bottom:1px solid #eee}.row:last-child{border-bottom:0}"
    "input[type=file]{width:100%}input[type=text],input[type=password]{width:100%;padding:10px;"
    "border:1px solid #ddd;border-radius:10px;margin:4px 0 10px;box-sizing:border-box}"
    "button{background:#8b5e3c;color:#fff;border:0;border-radius:10px;padding:10px 18px;font-size:15px}"
    "button.ghost{background:#eee;color:#444}form{display:inline}"
    "a{color:#8b5e3c}.tag{font-size:12px;color:#8a8378}"
    "</style></head><body><h1>MP4 阅读器</h1>";

static esp_err_t root_get(httpd_req_t *req)
{
    hbuf_t b;
    hbuf_init(&b, 32768);
    if (!b.buf) {
        return httpd_resp_send_500(req);
    }

    hbuf_printf(&b, "%s<p class=muted>ESP32-P4 电子书阅读器 · 上传的书籍会保存到 SD 卡 /books</p>", PAGE_HEAD);

    hbuf_printf(&b,
                "<h2>上传书籍</h2><div class=card>"
                "<form method=POST action=/upload enctype=multipart/form-data>"
                "<input type=file name=file accept='.txt,.epub'>"
                "<p><button>上传</button></p></form>"
                "<p class=tag>支持 TXT（UTF-8 / GBK / UTF-16）与 EPUB</p></div>");

    hbuf_printf(&b,
                "<h2>WiFi</h2><div class=card>"
                "<div class=row><span>状态</span><span>%s</span></div>"
                "<form method=POST action=/api/wifi>"
                "<input type=text name=ssid placeholder='WiFi 名称 (2.4G)' value='%s'>"
                "<input type=password name=pass placeholder='密码'>"
                "<button>保存并重连</button></form></div>",
                net_wifi_is_connected() ? net_wifi_get_ip() : "未连接", "");

    lib_book_t *books = malloc(sizeof(lib_book_t) * 64);
    int count = books ? storage_scan(books, 64) : 0;

    hbuf_printf(&b, "<h2>书库（%d 本）</h2><div class=card>", count);
    if (count == 0) {
        hbuf_printf(&b, "<p class=muted>暂无书籍</p>");
    }
    for (int i = 0; i < count; i++) {
        hbuf_printf(&b, "<div class=row><div><div>");
        html_escape(&b, books[i].title);
        hbuf_printf(&b, "</div><div class=tag>%s · %.1f MB · 已读 %d%%</div></div>",
                    books[i].type == BOOK_TYPE_EPUB ? "EPUB" : "TXT", books[i].size / 1048576.0,
                    books[i].percent / 10);
        hbuf_printf(&b,
                    "<form method=POST action=/api/delete onsubmit=\"return confirm('删除这本书？')\">"
                    "<input type=hidden name=path value='");
        html_escape(&b, books[i].path);
        hbuf_printf(&b, "'><button class=ghost>删除</button></form></div>");
    }
    hbuf_printf(&b, "</div>");
    free(books);

    uint64_t free_b = 0;
    uint64_t total_b = 0;
    storage_free_space(&free_b, &total_b);
    hbuf_printf(&b, "<p class=muted>SD 卡：可用 %.1f GB / 共 %.1f GB</p>", free_b / 1073741824.0,
                total_b / 1073741824.0);
    hbuf_printf(&b, "</body></html>");

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, b.buf, (ssize_t)b.len);
    free(b.buf);
    return err;
}

static esp_err_t books_get(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return httpd_resp_send_500(req);
    }
    lib_book_t *books = malloc(sizeof(lib_book_t) * 64);
    int count = books ? storage_scan(books, 64) : 0;
    for (int i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "title", books[i].title);
        cJSON_AddStringToObject(o, "path", books[i].path);
        cJSON_AddStringToObject(o, "type", books[i].type == BOOK_TYPE_EPUB ? "epub" : "txt");
        cJSON_AddNumberToObject(o, "size", books[i].size);
        cJSON_AddNumberToObject(o, "percent", books[i].percent / 10);
        cJSON_AddItemToArray(arr, o);
    }
    free(books);

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

static esp_err_t status_get(httpd_req_t *req)
{
    uint64_t free_b = 0;
    uint64_t total_b = 0;
    storage_free_space(&free_b, &total_b);

    cJSON *o = cJSON_CreateObject();
    if (!o) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(o, "wifi_connected", net_wifi_is_connected());
    cJSON_AddStringToObject(o, "ip", net_wifi_get_ip());
    cJSON_AddNumberToObject(o, "free_bytes", (double)free_b);
    cJSON_AddNumberToObject(o, "total_bytes", (double)total_b);

    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

static void sanitize_filename(char *name)
{
    char *base = name;
    for (char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (base != name) {
        memmove(name, base, strlen(base) + 1);
    }
    for (char *p = name; *p; p++) {
        if ((unsigned char)*p < 0x20 || *p == ':' || *p == '*' || *p == '?' || *p == '"' ||
            *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
}

static esp_err_t upload_post(httpd_req_t *req)
{
    char ctype[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing content-type");
    }
    const char *bp = strstr(ctype, "boundary=");
    if (!bp) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing boundary");
    }
    char boundary[80];
    snprintf(boundary, sizeof(boundary), "--%s", bp + 9);

    size_t body_len = 0;
    char *body = read_body(req, MAX_UPLOAD_BYTES, &body_len);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    }

    esp_err_t result = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no file");

    char *fn = strstr(body, "filename=\"");
    if (fn) {
        fn += 10;
        char *fn_end = strchr(fn, '"');
        if (fn_end) {
            char filename[128] = {0};
            size_t n = (size_t)(fn_end - fn);
            if (n >= sizeof(filename)) {
                n = sizeof(filename) - 1;
            }
            memcpy(filename, fn, n);
            sanitize_filename(filename);

            char *data = strstr(fn_end, "\r\n\r\n");
            if (data && filename[0]) {
                data += 4;
                char end_marker[96];
                snprintf(end_marker, sizeof(end_marker), "\r\n%s", boundary);
                char *data_end = strstr(data, end_marker);
                if (data_end) {
                    char path[256];
                    snprintf(path, sizeof(path), "/sdcard/books/%s", filename);
                    FILE *f = fopen(path, "wb");
                    if (f) {
                        size_t written = fwrite(data, 1, (size_t)(data_end - data), f);
                        fclose(f);
                        ESP_LOGI(TAG, "上传 %s（%u 字节）", path, (unsigned)written);
                        refresh_library_ui();
                        httpd_resp_set_status(req, "302 Found");
                        httpd_resp_set_hdr(req, "Location", "/");
                        result = httpd_resp_send(req, NULL, 0);
                    } else {
                        result = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
                    }
                }
            }
        }
    }

    free(body);
    return result;
}

static esp_err_t delete_post(httpd_req_t *req)
{
    size_t len = 0;
    char *body = read_body(req, 4096, &len);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char *path = strstr(body, "path=");
    if (!path) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing path");
    }
    path += 5;
    char *amp = strchr(path, '&');
    if (amp) {
        *amp = '\0';
    }
    url_decode_inplace(path);

    if (strncmp(path, "/sdcard/books/", 14) == 0) {
        storage_delete(path);
        refresh_library_ui();
    }
    free(body);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    size_t len = 0;
    char *body = read_body(req, 2048, &len);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    char *p = strstr(body, "ssid=");
    if (p) {
        p += 5;
        char *amp = strchr(p, '&');
        if (amp) {
            *amp = '\0';
        }
        url_decode_inplace(p);
        snprintf(ssid, sizeof(ssid), "%s", p);
        if (amp) {
            char *q = strstr(amp + 1, "pass=");
            if (q) {
                q += 5;
                url_decode_inplace(q);
                snprintf(pass, sizeof(pass), "%s", q);
            }
        }
    }
    free(body);

    if (ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    }

    net_wifi_set_credentials(ssid, pass);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
                              "<meta charset=utf-8><body style='font-family:sans-serif;padding:24px'>"
                              "<h3>WiFi 已保存，正在重连…</h3><p><a href='/'>返回</a></p></body>");
}

static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

void web_server_start(void)
{
    if (s_server) {
        return;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败");
        s_server = NULL;
        return;
    }

    httpd_uri_t uris[] = {
        {"/", HTTP_GET, root_get, NULL},
        {"/api/books", HTTP_GET, books_get, NULL},
        {"/api/status", HTTP_GET, status_get, NULL},
        {"/upload", HTTP_POST, upload_post, NULL},
        {"/api/delete", HTTP_POST, delete_post, NULL},
        {"/api/wifi", HTTP_POST, wifi_post, NULL},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, not_found);

    ESP_LOGI(TAG, "网页服务已启动: http://%s/", net_wifi_get_ip());
}
