#include "storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "storage";

#define NVS_NS_CFG "reader"
#define NVS_NS_PROG "prog"
#define NVS_NS_MARK "mark"
#define MAX_BOOKMARKS 8
#define MAX_SCAN_DEPTH 2

static bool s_sd_ready = false;
static reader_cfg_t s_cfg;

static const char *books_dir(void)
{
    static char path[128];
    if (path[0] == '\0') {
        snprintf(path, sizeof(path), "%s/books", BSP_SD_MOUNT_POINT);
    }
    return path;
}

static void ensure_dir(const char *subdir)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", BSP_SD_MOUNT_POINT, subdir);
    mkdir(path, 0777);
}

/* -------------------------------------------------------------------------- */

esp_err_t storage_init(void)
{
    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD 卡挂载失败: %s", esp_err_to_name(err));
        s_sd_ready = false;
        return err;
    }
    s_sd_ready = true;

    ensure_dir("books");
    ensure_dir("pictures");
    ensure_dir("music");
    ensure_dir("videos");
    ESP_LOGI(TAG, "SD 卡已挂载到 %s", BSP_SD_MOUNT_POINT);
    return ESP_OK;
}

bool storage_ready(void)
{
    return s_sd_ready;
}

static bool supported(const char *name)
{
    size_t n = strlen(name);
    return (n > 4 && strcasecmp(name + n - 4, ".txt") == 0) ||
           (n > 5 && strcasecmp(name + n - 5, ".epub") == 0) ||
           (n > 3 && strcasecmp(name + n - 3, ".md") == 0);
}

static int scan_dir(const char *dir, lib_book_t *out, int max, int count, int depth)
{
    DIR *d = opendir(dir);
    if (!d) {
        return count;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char path[384];
        int path_len = snprintf(path, sizeof(path), "%s/%.200s", dir, ent->d_name);
        if (path_len <= 0) {
            continue;
        }

        struct stat st;
        memset(&st, 0, sizeof(st));   /* stat 失败时下面仍会读 st_size，必须先清零 */
        bool is_dir = false;
        if (stat(path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        } else if (ent->d_type == DT_DIR) {
            is_dir = true;
        }

        if (is_dir) {
            if (depth < MAX_SCAN_DEPTH) {
                count = scan_dir(path, out, max, count, depth + 1);
            }
            continue;
        }

        if (!supported(ent->d_name)) {
            continue;
        }

        lib_book_t *b = &out[count];
        memset(b, 0, sizeof(*b));
        size_t plen = (size_t)path_len;
        if (plen >= sizeof(b->path)) {
            plen = sizeof(b->path) - 1;
        }
        memcpy(b->path, path, plen);
        b->path[plen] = '\0';
        book_name_from_path(path, b->title, sizeof(b->title));
        b->size = (uint32_t)(st.st_size > 0 ? st.st_size : 0);
        b->type = (strlen(ent->d_name) > 5 && strcasecmp(ent->d_name + strlen(ent->d_name) - 5, ".epub") == 0)
                      ? BOOK_TYPE_EPUB
                      : BOOK_TYPE_TXT;
        b->percent = settings_progress_percent(path);
        count++;
    }
    closedir(d);
    return count;
}

static int cmp_book(const void *a, const void *b)
{
    const lib_book_t *x = a;
    const lib_book_t *y = b;
    return strcmp(x->title, y->title);
}

int storage_scan(lib_book_t *out, int max)
{
    if (!s_sd_ready) {
        return 0;
    }
    int n = scan_dir(books_dir(), out, max, 0, 0);
    if (n > 1) {
        qsort(out, n, sizeof(lib_book_t), cmp_book);
    }
    return n;
}

static bool name_has_ext(const char *name, const char *const *exts, int ext_count)
{
    size_t n = strlen(name);
    for (int i = 0; i < ext_count; i++) {
        size_t e = strlen(exts[i]);
        if (n > e && strcasecmp(name + n - e, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int cmp_media(const void *a, const void *b)
{
    const media_file_t *x = a;
    const media_file_t *y = b;
    return strcmp(x->name, y->name);
}

int storage_scan_media(const char *subdir, const char *const *exts, int ext_count,
                       media_file_t *out, int max)
{
    if (!s_sd_ready || !subdir || !out || max <= 0) {
        return 0;
    }

    char dir[160];
    snprintf(dir, sizeof(dir), "%s/%s", BSP_SD_MOUNT_POINT, subdir);

    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (!name_has_ext(ent->d_name, exts, ext_count)) {
            continue;
        }

        char path[384];
        int path_len = snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (path_len <= 0) {
            continue;
        }

        struct stat st;
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
            continue;
        }

        media_file_t *m = &out[count];
        memset(m, 0, sizeof(*m));
        size_t plen = strlen(path);
        if (plen >= sizeof(m->path)) {
            plen = sizeof(m->path) - 1;
        }
        memcpy(m->path, path, plen);
        m->path[plen] = '\0';

        size_t nlen = strlen(ent->d_name);
        if (nlen >= sizeof(m->name)) {
            nlen = sizeof(m->name) - 1;
        }
        memcpy(m->name, ent->d_name, nlen);
        m->name[nlen] = '\0';
        m->size = (uint32_t)(st.st_size > 0 ? st.st_size : 0);
        count++;
    }
    closedir(d);

    if (count > 1) {
        qsort(out, (size_t)count, sizeof(media_file_t), cmp_media);
    }
    return count;
}

esp_err_t storage_delete(const char *path)
{
    if (!s_sd_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (remove(path) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void storage_free_space(uint64_t *free_bytes, uint64_t *total_bytes)
{
    if (free_bytes) {
        *free_bytes = 0;
    }
    if (total_bytes) {
        *total_bytes = 0;
    }
    if (!s_sd_ready) {
        return;
    }
    uint64_t total = 0;
    uint64_t freeb = 0;
    if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &freeb) == ESP_OK) {
        if (free_bytes) {
            *free_bytes = freeb;
        }
        if (total_bytes) {
            *total_bytes = total;
        }
    }
}

/* -------------------------------------------------------------------------- */

esp_err_t settings_init(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.theme = 0;
    s_cfg.font_idx = 1;
    s_cfg.line_space = 6;
    s_cfg.brightness = 80;

    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_cfg);
        if (nvs_get_blob(h, "cfg", &s_cfg, &len) != ESP_OK || len != sizeof(s_cfg)) {
            /* 使用默认值 */
        }
        nvs_close(h);
    }
    if (s_cfg.brightness < 10 || s_cfg.brightness > 100) {
        s_cfg.brightness = 80;
    }
    if (s_cfg.line_space > 40) {
        s_cfg.line_space = 6;
    }
    return ESP_OK;
}

reader_cfg_t *settings_get(void)
{
    return &s_cfg;
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "cfg", &s_cfg, sizeof(s_cfg));
    nvs_commit(h);
    nvs_close(h);
}

static uint32_t path_hash(const char *path)
{
    uint32_t h = 2166136261u;
    for (const char *p = path; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static void progress_key(const char *path, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%08x", (unsigned)path_hash(path));
}

uint32_t settings_progress_offset(const char *path)
{
    char key[16];
    progress_key(path, key, sizeof(key));
    uint32_t offset = 0;
    uint16_t percent = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_PROG, NVS_READONLY, &h) == ESP_OK) {
        uint8_t buf[6];
        size_t len = sizeof(buf);
        if (nvs_get_blob(h, key, buf, &len) == ESP_OK && len == sizeof(buf)) {
            memcpy(&offset, buf, 4);
            memcpy(&percent, buf + 4, 2);
        }
        nvs_close(h);
    }
    (void)percent;
    return offset;
}

uint16_t settings_progress_percent(const char *path)
{
    char key[16];
    progress_key(path, key, sizeof(key));
    uint16_t percent = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_PROG, NVS_READONLY, &h) == ESP_OK) {
        uint8_t buf[6];
        size_t len = sizeof(buf);
        if (nvs_get_blob(h, key, buf, &len) == ESP_OK && len == sizeof(buf)) {
            memcpy(&percent, buf + 4, 2);
        }
        nvs_close(h);
    }
    return percent;
}

void settings_set_progress(const char *path, uint32_t offset, uint16_t percent)
{
    char key[16];
    progress_key(path, key, sizeof(key));
    uint8_t buf[6];
    memcpy(buf, &offset, 4);
    memcpy(buf + 4, &percent, 2);

    nvs_handle_t h;
    if (nvs_open(NVS_NS_PROG, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, key, buf, sizeof(buf));
        nvs_commit(h);
        nvs_close(h);
    }
}

int settings_bookmarks_get(const char *path, reader_bookmark_t *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }
    char key[16];
    progress_key(path, key, sizeof(key));
    nvs_handle_t h;
    int count = 0;
    if (nvs_open(NVS_NS_MARK, NVS_READONLY, &h) == ESP_OK) {
        uint8_t buf[1 + MAX_BOOKMARKS * sizeof(reader_bookmark_t)];
        size_t len = sizeof(buf);
        if (nvs_get_blob(h, key, buf, &len) == ESP_OK && len >= 1) {
            count = buf[0];
            if (count > MAX_BOOKMARKS) {
                count = MAX_BOOKMARKS;
            }
            size_t need = 1 + (size_t)count * sizeof(reader_bookmark_t);
            if (len >= need) {
                if (count > max) {
                    count = max;
                }
                memcpy(out, buf + 1, (size_t)count * sizeof(reader_bookmark_t));
            } else {
                count = 0;
            }
        }
        nvs_close(h);
    }
    return count;
}

bool settings_bookmark_add(const char *path, uint32_t offset, const char *title)
{
    reader_bookmark_t items[MAX_BOOKMARKS];
    int count = settings_bookmarks_get(path, items, MAX_BOOKMARKS);
    if (count >= MAX_BOOKMARKS) {
        memmove(&items[0], &items[1], sizeof(items[0]) * (MAX_BOOKMARKS - 1));
        count = MAX_BOOKMARKS - 1;
    }
    items[count].offset = offset;
    snprintf(items[count].title, sizeof(items[count].title), "%s", title ? title : "");
    count++;

    uint8_t buf[1 + MAX_BOOKMARKS * sizeof(reader_bookmark_t)];
    buf[0] = (uint8_t)count;
    memcpy(buf + 1, items, (size_t)count * sizeof(reader_bookmark_t));

    char key[16];
    progress_key(path, key, sizeof(key));
    nvs_handle_t h;
    if (nvs_open(NVS_NS_MARK, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_blob(h, key, buf, 1 + (size_t)count * sizeof(reader_bookmark_t)) == ESP_OK;
    nvs_commit(h);
    nvs_close(h);
    return ok;
}

void settings_bookmarks_clear(const char *path)
{
    char key[16];
    progress_key(path, key, sizeof(key));
    nvs_handle_t h;
    if (nvs_open(NVS_NS_MARK, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
}
