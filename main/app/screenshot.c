#include "screenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define SHOT_W 480
#define SHOT_H 800

static const char *TAG = "shot";

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void print_b64(const uint8_t *data, size_t len)
{
    size_t out_len = (len + 2) / 3 * 4 + 1;
    char *out = heap_caps_malloc(out_len + 128, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) {
        return;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        int rem = 1;
        if (i + 1 < len) {
            v |= (uint32_t)data[i + 1] << 8;
            rem = 2;
        }
        if (i + 2 < len) {
            v |= data[i + 2];
            rem = 3;
        }
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = rem > 1 ? B64[(v >> 6) & 0x3F] : '=';
        out[o++] = rem > 2 ? B64[v & 0x3F] : '=';
    }
    out[o] = '\0';

    for (size_t i = 0; i < o; i += 512) {
        size_t n = o - i > 512 ? 512 : o - i;
        fwrite(out + i, 1, n, stdout);
        fputc('\n', stdout);
        /* 115200 波特下全速写会丢字节，分批 + 延时让 USB 串口缓冲排空 */
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    fflush(stdout);
    free(out);
}

static void shot_dump(void)
{
    esp_lcd_panel_handle_t panel = bsp_display_get_panel_handle();
    if (!panel) {
        ESP_LOGE(TAG, "no panel");
        return;
    }

    void *fb = NULL;
    if (esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) {
        ESP_LOGE(TAG, "no frame buffer");
        return;
    }

    /* 锁住 LVGL 并强制整屏重绘，等 DMA 刷完再读帧缓冲 */
    if (bsp_display_lock(-1) != ESP_OK) {
        ESP_LOGW(TAG, "获取 LVGL 锁失败，跳过截图");
        return;
    }
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 帧缓冲由 DMA 写入，CPU 读取前必须无效化 cache，
     * 否则读到的是 cache 里的旧行与新数据的混合体 */
    esp_cache_msync(fb, (size_t)SHOT_W * SHOT_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

    const uint16_t *px = (const uint16_t *)fb;
    size_t count = (size_t)SHOT_W * SHOT_H;

    uint8_t *rle = heap_caps_malloc(count * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rle) {
        bsp_display_unlock();
        return;
    }

    size_t o = 0;
    for (size_t i = 0; i < count;) {
        uint16_t c = px[i];
        size_t j = i + 1;
        while (j < count && px[j] == c && (j - i) < 65535) {
            j++;
        }
        size_t run = j - i;
        rle[o++] = (uint8_t)(c & 0xFF);
        rle[o++] = (uint8_t)(c >> 8);
        rle[o++] = (uint8_t)(run & 0xFF);
        rle[o++] = (uint8_t)(run >> 8);
        i = j;
    }
    bsp_display_unlock();

    printf("\nSHOT %d %d %u\n", SHOT_W, SHOT_H, (unsigned)o);
    print_b64(rle, o);
    printf("SHOTEND\n");
    fflush(stdout);
    free(rle);

    ESP_LOGI(TAG, "screenshot sent (%u bytes)", (unsigned)o);
}

static void shot_once(void)
{
    /* 截图期间关闭日志，避免其他任务的日志插进 base64 数据流 */
    esp_log_level_set("*", ESP_LOG_NONE);
    shot_dump();
    esp_log_level_set("*", ESP_LOG_INFO);
}

static void shot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));
    shot_once();
    vTaskDelete(NULL);
}

void screenshot_dump_serial(void)
{
    xTaskCreate(shot_task, "shot", 8192, NULL, 3, NULL);
}

/* 同步截图（调试命令用，必须从非 LVGL 任务调用） */
void screenshot_dump_now(void)
{
    shot_once();
}
