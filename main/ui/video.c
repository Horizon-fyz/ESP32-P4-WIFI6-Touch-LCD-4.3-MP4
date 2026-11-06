#include "video.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "app_stream_adapter.h"
#include "audio/player.h"
#include "ble.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/jpeg_decode.h"
#include "home.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"

static const char *TAG = "video";

#define MAX_VIDEOS 32
#define VID_W 480
#define VID_H 800
#define FRAME_BYTES (VID_W * VID_H * 2)
/* 单缓冲：LVGL 会按 src 指针缓存解码结果，换缓冲会导致画面不更新 */
#define FRAME_BUFFERS 1

static const char *const VIDEO_EXTS[] = {".mp4", ".avi"};

static lv_obj_t *s_screen;
static lv_obj_t *s_play_screen;
static lv_obj_t *s_img;
static lv_obj_t *s_info;
static lv_timer_t *s_watch;

static media_file_t s_files[MAX_VIDEOS];
static int s_count;

static app_stream_adapter_handle_t s_adapter;
static esp_codec_dev_handle_t s_codec;
static uint8_t *s_fb[FRAME_BUFFERS];
static lv_image_dsc_t s_dsc;
static volatile bool s_playing;
static bool s_img_ready;
static bool s_info_shown;
static uint32_t s_last_frames;
static int s_stable_checks;
static uint32_t s_frame_w;
static uint32_t s_frame_h;

static void refresh_cb(lv_event_t *e);

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    ui_home_show();
}

static esp_err_t frame_cb(uint8_t *buffer, uint32_t buffer_size, uint32_t width, uint32_t height,
                          uint32_t frame_index, void *user_data)
{
    (void)buffer_size;
    (void)frame_index;
    (void)user_data;
    if (!s_playing || !s_img) {
        return ESP_OK;
    }
    if (bsp_display_lock(100) != ESP_OK) {
        return ESP_OK;
    }

    s_dsc.data = buffer;
    s_dsc.data_size = width * height * 2;
    s_dsc.header.w = (uint32_t)width;
    s_dsc.header.h = (uint32_t)height;
    s_dsc.header.stride = (uint32_t)width * 2;

    /* 尺寸只在首帧或分辨率变化时调整，避免每帧都触发布局计算 */
    if (width != s_frame_w || height != s_frame_h || !s_img_ready) {
        s_frame_w = width;
        s_frame_h = height;
        lv_obj_set_size(s_img, (int32_t)width, (int32_t)height);
        lv_obj_align(s_img, LV_ALIGN_CENTER, 0, -20);
        /* 首帧有效后才绑定 src，否则 LVGL 会用 0x0 的头部缓存住空图 */
        lv_image_set_src(s_img, &s_dsc);
        s_img_ready = true;
    }
    lv_obj_invalidate(s_img);

    bsp_display_unlock();
    return ESP_OK;
}

static void stop_playback(void)
{
    s_playing = false;

    if (s_watch) {
        lv_timer_delete(s_watch);
        s_watch = NULL;
    }

    if (s_adapter) {
        app_stream_adapter_stop(s_adapter);
        app_stream_adapter_deinit(s_adapter);
        s_adapter = NULL;
    }

    if (s_codec) {
        esp_codec_dev_close(s_codec);
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }

    for (int i = 0; i < FRAME_BUFFERS; i++) {
        if (s_fb[i]) {
            heap_caps_free(s_fb[i]);
            s_fb[i] = NULL;
        }
    }

    if (s_play_screen && s_dsc.header.magic == LV_IMAGE_HEADER_MAGIC) {
        lv_image_cache_drop(&s_dsc);
    }
}

static void play_back_cb(lv_event_t *e)
{
    (void)e;
    s_img = NULL;
    s_info = NULL;
    stop_playback();
    if (s_play_screen) {
        lv_obj_delete(s_play_screen);
        s_play_screen = NULL;
    }
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

static void watch_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_adapter || !s_playing) {
        return;
    }
    app_stream_stats_t stats;
    if (app_stream_adapter_get_stats(s_adapter, &stats) != ESP_OK) {
        return;
    }

    /* 分辨率要等抽取任务解析完容器后才有，首帧之后再补标题 */
    if (!s_info_shown) {
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t fps = 0;
        uint32_t duration = 0;
        if (app_stream_adapter_get_info(s_adapter, &w, &h, &fps, &duration) == ESP_OK) {
            char info[96];
            snprintf(info, sizeof(info), "%ux%u · %u fps", (unsigned)w, (unsigned)h,
                     (unsigned)fps);
            lv_label_set_text(s_info, info);
            s_info_shown = true;
        }
    }
    if (stats.frames_processed == s_last_frames) {
        s_stable_checks++;
        if (s_stable_checks >= 3) {
            /* 播完停在最后一帧，由用户点「停止」返回，避免画面突然消失 */
            s_playing = false;
            lv_label_set_text(s_info, "播放结束");
            lv_timer_delete(timer);
            s_watch = NULL;
        }
    } else {
        s_stable_checks = 0;
        s_last_frames = stats.frames_processed;
    }
}

static void start_playback(const media_file_t *file)
{
    audio_shutdown();

    s_codec = bsp_audio_codec_speaker_init();
    if (!s_codec) {
        ESP_LOGW(TAG, "音频不可用，仅播放画面");
    }

    for (int i = 0; i < FRAME_BUFFERS; i++) {
        /* 硬件 JPEG 解码要求输出缓冲按 cache line 对齐，必须用官方分配器 */
        jpeg_decode_memory_alloc_cfg_t mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
        };
        size_t actual = 0;
        s_fb[i] = jpeg_alloc_decoder_mem(FRAME_BYTES, &mem_cfg, &actual);
        if (!s_fb[i]) {
            ESP_LOGE(TAG, "帧缓冲分配失败");
            stop_playback();
            ui_toast("内存不足，无法播放视频");
            return;
        }
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    /* 硬件 JPEG 输出的 RGB565 字节序与 LVGL 原生相反，用 SWAPPED 格式免去逐帧转换 */
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;

    s_play_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_play_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_play_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_play_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_play_screen, 0, 0);

    s_img = lv_image_create(s_play_screen);
    lv_obj_center(s_img);

    lv_obj_t *back = ui_button(s_play_screen, "停止", 88, 46, play_back_cb, NULL);
    lv_obj_set_pos(back, 12, 12);

    s_info = ui_label(s_play_screen, file->name, fonts_ui(), lv_color_hex(0xFFFFFF));
    lv_obj_set_width(s_info, 340);
    lv_obj_set_height(s_info, 43);
    lv_obj_align(s_info, LV_ALIGN_TOP_RIGHT, -14, 24);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_MODE_DOTS);

    app_stream_adapter_config_t config;
    memset(&config, 0, sizeof(config));
    config.frame_cb = frame_cb;
    config.user_data = NULL;
    config.decode_buffers = (void **)s_fb;
    config.buffer_count = FRAME_BUFFERS;
    config.buffer_size = FRAME_BYTES;
    config.audio_dev = s_codec;
    config.jpeg_config.output_format = APP_STREAM_JPEG_OUTPUT_RGB565;
    config.jpeg_config.bgr_order = false;

    esp_err_t ret = app_stream_adapter_init(&config, &s_adapter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "适配器初始化失败: %s", esp_err_to_name(ret));
        stop_playback();
        lv_obj_delete(s_play_screen);
        s_play_screen = NULL;
        s_img = NULL;
        ui_toast("视频模块初始化失败");
        return;
    }

    ret = app_stream_adapter_set_file(s_adapter, file->path, s_codec != NULL);
    if (ret != ESP_OK) {
        stop_playback();
        lv_obj_delete(s_play_screen);
        s_play_screen = NULL;
        s_img = NULL;
        ui_toast("不是支持的视频（需 MJPEG 编码的 MP4/AVI）");
        return;
    }

    s_playing = true;
    s_img_ready = false;
    s_info_shown = false;
    s_last_frames = 0;
    s_stable_checks = 0;
    s_frame_w = 0;
    s_frame_h = 0;

    lv_screen_load(s_play_screen);

    ret = app_stream_adapter_start(s_adapter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动播放失败: %s", esp_err_to_name(ret));
        ui_toast("启动播放失败");
        play_back_cb(NULL);
        return;
    }

    s_watch = lv_timer_create(watch_cb, 500, NULL);
}

static void item_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index >= 0 && index < s_count) {
        start_playback(&s_files[index]);
    }
}

static void rebuild(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_clean(s_screen);
    const ui_theme_t *t = ui_theme();

    lv_obj_t *back = ui_button(s_screen, "首页", 84, 46, back_home_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "视频", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    char count[48];
    snprintf(count, sizeof(count), "%d 个", s_count);
    lv_obj_t *badge = ui_label(s_screen, count, fonts_ui(), t->muted);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -110, 28);

    lv_obj_t *refresh = ui_button(s_screen, "刷新", 84, 46, refresh_cb, NULL);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -20, 20);

    lv_obj_t *hint = ui_label(s_screen, "SD:/videos · MJPEG 编码的 MP4 / AVI", fonts_ui(), t->muted);
    lv_obj_set_pos(hint, 22, 80);
    lv_obj_set_width(hint, 436);

    lv_obj_t *list = lv_obj_create(s_screen);
    lv_obj_set_size(list, 448, 640);
    lv_obj_set_pos(list, 16, 116);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    if (s_count == 0) {
        lv_obj_t *empty = ui_label(list,
                                   "没有视频\n\n把 .mp4 / .avi 放到 SD 卡\n/videos 目录\n\n"
                                   "（编码需为 MJPEG，H.264 不支持）",
                                   fonts_ui(), t->muted);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(empty, 6, 0);
        lv_obj_set_style_pad_top(empty, 30, 0);
        return;
    }

    for (int i = 0; i < s_count; i++) {
        lv_obj_t *btn = lv_button_create(list);
        lv_obj_set_size(btn, LV_PCT(100), 96);
        lv_obj_set_style_bg_color(btn, t->panel, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t *name = ui_label(btn, s_files[i].name, fonts_ui(), t->fg);
        lv_obj_set_pos(name, 18, 6);
        lv_obj_set_width(name, 380);
        lv_obj_set_height(name, 43);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        char meta[48];
        snprintf(meta, sizeof(meta), "%.1f MB", s_files[i].size / 1048576.0);
        lv_obj_t *sub = ui_label(btn, meta, fonts_ui(), t->muted);
        lv_obj_set_pos(sub, 18, 50);

        lv_obj_add_event_cb(btn, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    if (!storage_ready()) {
        storage_init();
    }
    s_count = storage_scan_media("videos", VIDEO_EXTS, 2, s_files, MAX_VIDEOS);
    rebuild();
    ui_toast("已刷新");
}

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    s_screen = NULL;
}

void ui_video_show(void)
{
    if (ble_is_ready()) {
        ble_stop();
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_screen = ui_screen_new();
    lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    s_count = storage_scan_media("videos", VIDEO_EXTS, 2, s_files, MAX_VIDEOS);
    rebuild();

    lv_screen_load(s_screen);
}

void ui_video_play_file(const char *path)
{
    if (!path) {
        return;
    }
    if (!s_screen) {
        s_screen = ui_screen_new();
        lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);
        lv_screen_load(s_screen);
    }
    media_file_t file;
    memset(&file, 0, sizeof(file));
    snprintf(file.path, sizeof(file.path), "%.*s", (int)sizeof(file.path) - 1, path);
    const char *base = strrchr(path, '/');
    snprintf(file.name, sizeof(file.name), "%s", base ? base + 1 : path);
    start_playback(&file);
}
