#include "debug_cmd.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio/player.h"
#include "ble.h"
#include "ble_screen.h"
#include "bsp/esp-bsp.h"
#include "clock_screen.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery.h"
#include "home.h"
#include "library.h"
#include "music_screen.h"
#include "screenshot.h"
#include "settings_screen.h"
#include "storage/storage.h"
#include "theme.h"
#include "video.h"
#include "wifi.h"
#include "wifi_screen.h"

static const char *TAG = "dbg";

static void open_screen(const char *name)
{
    bsp_display_lock(-1);
    if (strcmp(name, "home") == 0) {
        ui_home_show();
    } else if (strcmp(name, "library") == 0) {
        ui_library_show();
    } else if (strcmp(name, "gallery") == 0) {
        ui_gallery_show();
    } else if (strcmp(name, "music") == 0) {
        ui_music_show();
    } else if (strcmp(name, "video") == 0) {
        ui_video_show();
    } else if (strcmp(name, "wifi") == 0) {
        ui_wifi_show();
    } else if (strcmp(name, "ble") == 0) {
        ui_ble_show();
    } else if (strcmp(name, "clock") == 0) {
        ui_clock_show();
    } else if (strcmp(name, "settings") == 0) {
        ui_settings_show();
    } else {
        printf("DBG unknown screen: %s\n", name);
    }
    bsp_display_unlock();
}

static void list_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        printf("DBG ls %s: open failed\n", dir);
        return;
    }
    printf("DBG ls %s:\n", dir);
    struct dirent *ent;
    int n = 0;
    while ((ent = readdir(d)) != NULL && n < 40) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char path[320];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        memset(&st, 0, sizeof(st));
        stat(path, &st);
        printf("DBG   %s %u\n", ent->d_name, (unsigned)st.st_size);
        n++;
    }
    closedir(d);
    printf("DBG ls done\n");
}

static void wifi_scan(void)
{
    net_wifi_ap_t aps[16];
    int n = net_wifi_scan(aps, 16);
    printf("DBG wifiscan: %d AP\n", n);
    for (int i = 0; i < n; i++) {
        printf("DBG   %s %d dBm auth=%d\n", aps[i].ssid, aps[i].rssi, aps[i].authmode);
    }
    printf("DBG wifiscan done\n");
}

static void ble_scan(void)
{
    if (!ble_is_ready()) {
        printf("DBG ble init: %s\n", esp_err_to_name(ble_start()));
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    printf("DBG ble state: %s\n", ble_state_text());
    ble_scan_start();
    vTaskDelay(pdMS_TO_TICKS(6000));
    ble_device_t devs[8];
    int n = ble_get_devices(devs, 8);
    printf("DBG blescan: %d dev\n", n);
    for (int i = 0; i < n; i++) {
        printf("DBG   %s rssi=%d\n", devs[i].name, devs[i].rssi);
    }
    printf("DBG blescan done\n");
}

/* 生成 2 秒 440Hz 立体声 WAV，用于验证 ES8311 输出 */
static void make_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("DBG wavtest: open failed\n");
        return;
    }
    const uint32_t rate = 44100;
    const uint16_t channels = 2;
    const uint16_t bits = 16;
    const uint32_t frames = rate * 2;
    const uint32_t data_size = frames * channels * (bits / 8);

    uint8_t hdr[44] = {0};
    memcpy(hdr, "RIFF", 4);
    uint32_t riff = 36 + data_size;
    memcpy(hdr + 4, &riff, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    uint32_t fmt = 16;
    memcpy(hdr + 16, &fmt, 4);
    uint16_t pcm = 1;
    memcpy(hdr + 20, &pcm, 2);
    memcpy(hdr + 22, &channels, 2);
    memcpy(hdr + 24, &rate, 4);
    uint32_t byte_rate = rate * channels * (bits / 8);
    memcpy(hdr + 28, &byte_rate, 4);
    uint16_t block_align = channels * (bits / 8);
    memcpy(hdr + 32, &block_align, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_size, 4);
    fwrite(hdr, 1, sizeof(hdr), f);

    int16_t *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        return;
    }
    for (uint32_t done = 0; done < frames;) {
        int n = 0;
        for (; n < 512 && done + (uint32_t)n < frames; n++, done++) {
            int16_t v = (int16_t)(sinf(2.0f * (float)M_PI * 440.0f * done / rate) * 8000.0f);
            buf[n * 2] = v;
            buf[n * 2 + 1] = v;
        }
        fwrite(buf, sizeof(int16_t), (size_t)n * 2, f);
    }
    free(buf);
    fclose(f);
    printf("DBG wav written: %s\n", path);
}

/* 生成 480x800 渐变 BMP，用于验证图片浏览 */
static void make_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("DBG bmptest: open failed\n");
        return;
    }
    const int w = 480;
    const int h = 800;
    const int row = (w * 3 + 3) & ~3;
    const uint32_t image_size = (uint32_t)row * h;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    uint32_t total = 54 + image_size;
    memcpy(hdr + 2, &total, 4);
    uint32_t off = 54;
    memcpy(hdr + 10, &off, 4);
    uint32_t info = 40;
    memcpy(hdr + 14, &info, 4);
    int32_t iw = w;
    int32_t ih = h;
    memcpy(hdr + 18, &iw, 4);
    memcpy(hdr + 22, &ih, 4);
    uint16_t planes = 1;
    memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 24;
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &image_size, 4);
    fwrite(hdr, 1, sizeof(hdr), f);

    uint8_t *line = malloc((size_t)row);
    if (!line) {
        fclose(f);
        return;
    }
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            line[x * 3 + 0] = (uint8_t)(x * 255 / w);
            line[x * 3 + 1] = (uint8_t)(y * 255 / h);
            line[x * 3 + 2] = (uint8_t)((x + y) % 256);
        }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line);
    fclose(f);
    printf("DBG bmp written: %s\n", path);
}

/* 测试素材由 CMake EMBED_FILES 嵌入 */
extern const uint8_t test_jpg_start[] asm("_binary_test_jpg_start");
extern const uint8_t test_jpg_end[] asm("_binary_test_jpg_end");
extern const uint8_t test_avi_start[] asm("_binary_test_avi_start");
extern const uint8_t test_avi_end[] asm("_binary_test_avi_end");

static void write_asset(const char *path, const uint8_t *start, const uint8_t *end)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("DBG mktest: open %s failed\n", path);
        return;
    }
    size_t n = (size_t)(end - start);
    size_t w = fwrite(start, 1, n, f);
    fclose(f);
    printf("DBG wrote %s (%u/%u bytes)\n", path, (unsigned)w, (unsigned)n);
}

static void make_test_assets(void)
{
    if (!storage_ready()) {
        storage_init();
    }
    write_asset("/sdcard/pictures/test.jpg", test_jpg_start, test_jpg_end);
    write_asset("/sdcard/videos/test.avi", test_avi_start, test_avi_end);
}

static void handle(char *line)
{
    char *cmd = strtok(line, " \r\n");
    if (!cmd) {
        return;
    }
    /* 丢掉串口打开瞬间的噪声字节，只保留字母开头的命令 */
    while (*cmd && !((*cmd >= 'a' && *cmd <= 'z') || (*cmd >= 'A' && *cmd <= 'Z'))) {
        cmd++;
    }
    if (!*cmd) {
        return;
    }

    if (strcmp(cmd, "ui") == 0) {
        char *arg = strtok(NULL, " \r\n");
        open_screen(arg ? arg : "home");
        printf("DBG ui %s\n", arg ? arg : "home");
    } else if (strcmp(cmd, "shot") == 0) {
        printf("DBG shot begin\n");
        screenshot_dump_now();
        printf("DBG shot done\n");
    } else if (strcmp(cmd, "theme") == 0) {
        char *arg = strtok(NULL, " \r\n");
        int idx = arg ? atoi(arg) : 0;
        /* 模拟真实路径：先进入设置页（首页变为非活动屏），再切主题 */
        open_screen("settings");
        bsp_display_lock(-1);
        ui_theme_set_index(idx);
        ui_home_reset();
        ui_library_reset();
        bsp_display_unlock();
        open_screen("home");
        printf("DBG theme %d\n", idx);
    } else if (strcmp(cmd, "ls") == 0) {
        char *arg = strtok(NULL, " \r\n");
        list_dir(arg ? arg : "/sdcard");
    } else if (strcmp(cmd, "wifiscan") == 0) {
        wifi_scan();
    } else if (strcmp(cmd, "heap") == 0) {
        printf("DBG heap internal=%u largest_int=%u dma=%u psram=%u largest_psram=%u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    } else if (strcmp(cmd, "ble") == 0) {
        audio_shutdown();
        printf("DBG ble init: %s\n", esp_err_to_name(ble_start()));
        vTaskDelay(pdMS_TO_TICKS(6000));
        printf("DBG ble state: %s\n", ble_state_text());
    } else if (strcmp(cmd, "blescan") == 0) {
        ble_scan();
    } else if (strcmp(cmd, "wavtest") == 0) {
        if (ble_is_ready()) {
            ble_stop();
        }
        if (!storage_ready()) {
            storage_init();
        }
        make_wav("/sdcard/music/test.wav");
        printf("DBG audio init: %s\n", esp_err_to_name(audio_init()));
        printf("DBG play: %s\n", esp_err_to_name(audio_play_file("/sdcard/music/test.wav")));
        vTaskDelay(pdMS_TO_TICKS(1500));
        printf("DBG playing=%d\n", (int)audio_is_playing());
    } else if (strcmp(cmd, "bmptest") == 0) {
        if (!storage_ready()) {
            storage_init();
        }
        make_bmp("/sdcard/pictures/test.bmp");
    } else if (strcmp(cmd, "mktest") == 0) {
        make_test_assets();
    } else if (strcmp(cmd, "vidplay") == 0) {
        char *arg = strtok(NULL, "\r\n");
        bsp_display_lock(-1);
        ui_video_play_file(arg ? arg : "/sdcard/videos/test.avi");
        bsp_display_unlock();
        printf("DBG vidplay %s\n", arg ? arg : "test.avi");
    } else if (strcmp(cmd, "gview") == 0) {
        char *arg = strtok(NULL, " \r\n");
        bsp_display_lock(-1);
        ui_gallery_open_index(arg ? atoi(arg) : 0);
        bsp_display_unlock();
        printf("DBG gview %s\n", arg ? arg : "0");
    } else if (strcmp(cmd, "play") == 0) {
        char *path = strtok(NULL, "\r\n");
        if (ble_is_ready()) {
            ble_stop();
        }
        audio_init();
        printf("DBG play: %s\n", esp_err_to_name(audio_play_file(path)));
    } else if (strcmp(cmd, "stop") == 0) {
        audio_stop();
        printf("DBG stop\n");
    } else if (strcmp(cmd, "help") == 0) {
        printf("DBG cmds: ui <home|library|gallery|music|video|wifi|ble|clock|settings> | shot | "
               "theme <0-2> | ls <dir> | wifiscan | ble | blescan | wavtest | bmptest | play <path> | stop\n");
    } else {
        printf("DBG unknown cmd: %s\n", cmd);
    }
}

static void cmd_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "调试命令已启动（输入 help 查看）");

    char line[160];
    size_t len = 0;
    uint8_t ch;
    while (true) {
        int n = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200));
        if (n <= 0) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (len > 0) {
                line[len] = '\0';
                handle(line);
                fflush(stdout);
                len = 0;
            }
            continue;
        }
        if (ch == 0x7F || ch == 0x08) { /* 退格 */
            if (len > 0) {
                len--;
            }
            continue;
        }
        if (len < sizeof(line) - 1) {
            line[len++] = (char)ch;
        }
    }
}

void debug_cmd_start(void)
{
    /* UART0 默认只有输出，安装驱动后才能读命令（也用于自动化测试） */
    uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
    xTaskCreate(cmd_task, "dbgcmd", 8192, NULL, 2, NULL);
}
