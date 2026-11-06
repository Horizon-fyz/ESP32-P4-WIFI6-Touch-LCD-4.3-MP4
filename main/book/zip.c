#include "zip.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "zlib.h"

static const char *TAG = "zip";

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool zip_open(zip_t *z, const char *path)
{
    memset(z, 0, sizeof(*z));
    z->fp = fopen(path, "rb");
    if (!z->fp) {
        ESP_LOGE(TAG, "无法打开 %s", path);
        return false;
    }

    if (fseek(z->fp, 0, SEEK_END) != 0) {
        zip_close(z);
        return false;
    }
    long size = ftell(z->fp);
    if (size < 22) {
        zip_close(z);
        return false;
    }

    size_t tail = (size_t)size > 66560 ? 66560 : (size_t)size;
    uint8_t *buf = malloc(tail);
    if (!buf) {
        zip_close(z);
        return false;
    }
    fseek(z->fp, size - (long)tail, SEEK_SET);
    if (fread(buf, 1, tail, z->fp) != tail) {
        free(buf);
        zip_close(z);
        return false;
    }

    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; i--) {
        if (rd32(buf + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        ESP_LOGE(TAG, "未找到 EOCD");
        free(buf);
        zip_close(z);
        return false;
    }

    uint16_t total = rd16(buf + eocd + 10);
    uint32_t cd_size = rd32(buf + eocd + 12);
    uint32_t cd_off = rd32(buf + eocd + 16);
    free(buf);

    if (total == 0xFFFF || cd_off == 0xFFFFFFFF || cd_size == 0xFFFFFFFF) {
        ESP_LOGW(TAG, "暂不支持 ZIP64");
        zip_close(z);
        return false;
    }

    if (cd_off + cd_size > (uint32_t)size) {
        zip_close(z);
        return false;
    }

    uint8_t *cd = malloc(cd_size);
    if (!cd) {
        zip_close(z);
        return false;
    }
    fseek(z->fp, cd_off, SEEK_SET);
    if (fread(cd, 1, cd_size, z->fp) != cd_size) {
        free(cd);
        zip_close(z);
        return false;
    }

    size_t pos = 0;
    while (pos + 46 <= cd_size && z->count < ZIP_MAX_ENTRIES) {
        if (rd32(cd + pos) != 0x02014b50) {
            break;
        }
        zip_entry_t *e = &z->entries[z->count];
        e->method = rd16(cd + pos + 10);
        e->comp_size = rd32(cd + pos + 20);
        e->uncomp_size = rd32(cd + pos + 24);
        uint16_t name_len = rd16(cd + pos + 28);
        uint16_t extra_len = rd16(cd + pos + 30);
        uint16_t comment_len = rd16(cd + pos + 32);
        e->local_offset = rd32(cd + pos + 42);

        size_t n = name_len < ZIP_MAX_NAME - 1 ? name_len : ZIP_MAX_NAME - 1;
        memcpy(e->name, cd + pos + 46, n);
        e->name[n] = '\0';
        for (size_t k = 0; k < n; k++) {
            if (e->name[k] == '\\') {
                e->name[k] = '/';
            }
        }

        z->count++;
        pos += 46u + name_len + extra_len + comment_len;
    }

    free(cd);
    ESP_LOGI(TAG, "%s: %d 个条目", path, z->count);
    return z->count > 0;
}

void zip_close(zip_t *z)
{
    if (z->fp) {
        fclose(z->fp);
        z->fp = NULL;
    }
    z->count = 0;
}

int zip_find(zip_t *z, const char *name)
{
    for (int i = 0; i < z->count; i++) {
        if (strcmp(z->entries[i].name, name) == 0) {
            return i;
        }
    }
    for (int i = 0; i < z->count; i++) {
        if (strcasecmp(z->entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t zip_read_entry(zip_t *z, int idx, char **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    if (idx < 0 || idx >= z->count) {
        return ESP_ERR_INVALID_ARG;
    }
    zip_entry_t *e = &z->entries[idx];

    if (e->uncomp_size > 32 * 1024 * 1024) {
        ESP_LOGE(TAG, "条目过大: %s", e->name);
        return ESP_ERR_INVALID_SIZE;
    }

    if (fseek(z->fp, (long)e->local_offset, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    uint8_t lh[30];
    if (fread(lh, 1, sizeof(lh), z->fp) != sizeof(lh) || rd32(lh) != 0x04034b50) {
        return ESP_FAIL;
    }
    uint16_t name_len = rd16(lh + 26);
    uint16_t extra_len = rd16(lh + 28);
    fseek(z->fp, (long)(name_len + extra_len), SEEK_CUR);

    uint8_t *in = malloc(e->comp_size ? e->comp_size : 1);
    char *dst = heap_caps_malloc(e->uncomp_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dst) {
        dst = malloc(e->uncomp_size + 1);
    }
    if (!in || !dst) {
        free(in);
        free(dst);
        return ESP_ERR_NO_MEM;
    }
    if (e->comp_size && fread(in, 1, e->comp_size, z->fp) != e->comp_size) {
        free(in);
        free(dst);
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    if (e->method == 0) {
        if (e->comp_size < e->uncomp_size) {
            err = ESP_FAIL;
        } else {
            memcpy(dst, in, e->uncomp_size);
        }
    } else if (e->method == 8) {
        z_stream strm = {0};
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
            err = ESP_FAIL;
        } else {
            strm.next_in = in;
            strm.avail_in = e->comp_size;
            strm.next_out = (Bytef *)dst;
            strm.avail_out = e->uncomp_size;
            int ret = inflate(&strm, Z_FINISH);
            if (ret != Z_STREAM_END && ret != Z_OK) {
                ESP_LOGE(TAG, "inflate 失败 %d: %s", ret, e->name);
                err = ESP_FAIL;
            }
            inflateEnd(&strm);
        }
    } else {
        ESP_LOGE(TAG, "不支持的压缩方式 %u: %s", e->method, e->name);
        err = ESP_ERR_NOT_SUPPORTED;
    }

    free(in);
    if (err != ESP_OK) {
        free(dst);
        return err;
    }

    dst[e->uncomp_size] = '\0';
    *out = dst;
    *out_len = e->uncomp_size;
    return ESP_OK;
}
