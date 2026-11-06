#include "gallery.h"

#include <stdio.h>
#include <string.h>

#include "app/fonts.h"
#include "ble.h"
#include "esp_lv_decoder.h"
#include "esp_log.h"
#include "home.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "storage/storage.h"
#include "theme.h"
#include "widgets.h"

#define MAX_IMAGES 64
#define SCALE_MIN 128   /* 0.5x */
#define SCALE_MAX 768   /* 3x */
#define SCALE_STEP 64

static const char *const IMAGE_EXTS[] = {".jpg", ".jpeg", ".png", ".bmp", ".qoi"};

static lv_obj_t *s_screen;
static lv_obj_t *s_view;
static lv_obj_t *s_view_img;
static lv_obj_t *s_view_index;

static media_file_t s_files[MAX_IMAGES];
static int s_count;
static int s_index;
static int s_scale = 256;
static esp_lv_decoder_handle_t s_decoder;

static void refresh_cb(lv_event_t *e);

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    ui_home_show();
}

/* "/sdcard/pictures/a.jpg" -> "S:/pictures/a.jpg"
 * LV_FS_POSIX_PATH 配成 "/sdcard"，LVGL 会直接拼接后面的 "/pictures/a.jpg" */
static void to_lv_path(const char *path, char *out, size_t out_size)
{
    const char prefix[] = "/sdcard";
    if (strncmp(path, prefix, sizeof(prefix) - 1) == 0) {
        snprintf(out, out_size, "S:%.*s", (int)(out_size - 3), path + sizeof(prefix) - 1);
    } else {
        snprintf(out, out_size, "S:/%.*s", (int)(out_size - 4), path);
    }
}

static void close_view(void)
{
    if (s_view) {
        lv_obj_delete(s_view);
        s_view = NULL;
        s_view_img = NULL;
        s_view_index = NULL;
        lv_image_cache_drop(NULL);
    }
}

static void view_back_cb(lv_event_t *e)
{
    (void)e;
    close_view();
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

static void update_index_label(void)
{
    if (!s_view_index) {
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", s_index + 1, s_count);
    lv_label_set_text(s_view_index, buf);
}

static void show_index(int index)
{
    if (s_count <= 0) {
        return;
    }
    if (index < 0) {
        index = s_count - 1;
    } else if (index >= s_count) {
        index = 0;
    }
    s_index = index;

    char lv_path[224];
    to_lv_path(s_files[s_index].path, lv_path, sizeof(lv_path));

    lv_image_header_t header;
    if (lv_image_decoder_get_info(lv_path, &header) != LV_RESULT_OK) {
        ui_toast("不支持的图片格式或文件已损坏");
        return;
    }
    lv_image_set_src(s_view_img, lv_path);
    s_scale = 256;
    lv_image_set_scale(s_view_img, s_scale);
    lv_obj_center(s_view_img);
    update_index_label();
}

static void prev_cb(lv_event_t *e)
{
    (void)e;
    show_index(s_index - 1);
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    show_index(s_index + 1);
}

static void zoom_in_cb(lv_event_t *e)
{
    (void)e;
    if (s_scale + SCALE_STEP <= SCALE_MAX) {
        s_scale += SCALE_STEP;
        lv_image_set_scale(s_view_img, s_scale);
    }
}

static void zoom_out_cb(lv_event_t *e)
{
    (void)e;
    if (s_scale - SCALE_STEP >= SCALE_MIN) {
        s_scale -= SCALE_STEP;
        lv_image_set_scale(s_view_img, s_scale);
    }
}

static void reset_zoom_cb(lv_event_t *e)
{
    (void)e;
    s_scale = 256;
    lv_image_set_scale(s_view_img, s_scale);
    lv_obj_center(s_view_img);
}

static lv_point_t s_press_point;

static void img_pressed_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_get_point(lv_indev_active(), &s_press_point);
}

static void img_released_cb(lv_event_t *e)
{
    (void)e;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int dx = p.x - s_press_point.x;
    if (dx > 70) {
        show_index(s_index - 1);
    } else if (dx < -70) {
        show_index(s_index + 1);
    }
}

static void open_viewer(int index)
{
    const ui_theme_t *t = ui_theme();
    close_view();

    s_view = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_view, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_view, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_view, 0, 0);

    s_view_img = lv_image_create(s_view);
    lv_obj_add_flag(s_view_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_view_img, img_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_view_img, img_released_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *back = ui_button(s_view, "返回", 84, 46, view_back_cb, NULL);
    lv_obj_set_pos(back, 12, 12);

    s_view_index = ui_label(s_view, "", fonts_ui(), lv_color_hex(0xFFFFFF));
    lv_obj_align(s_view_index, LV_ALIGN_TOP_RIGHT, -16, 24);

    lv_obj_t *bar = lv_obj_create(s_view);
    lv_obj_set_size(bar, 480, 84);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *prev = ui_button(bar, "上一张", 92, 56, prev_cb, NULL);
    lv_obj_set_pos(prev, 8, 4);
    lv_obj_t *next = ui_button(bar, "下一张", 92, 56, next_cb, NULL);
    lv_obj_set_pos(next, 108, 4);
    lv_obj_t *zoom_out = ui_button(bar, "缩小", 84, 56, zoom_out_cb, NULL);
    lv_obj_set_pos(zoom_out, 212, 4);
    lv_obj_t *zoom_in = ui_button(bar, "放大", 84, 56, zoom_in_cb, NULL);
    lv_obj_set_pos(zoom_in, 304, 4);
    lv_obj_t *reset = ui_button(bar, "复位", 76, 56, reset_zoom_cb, NULL);
    lv_obj_set_pos(reset, 396, 4);
    (void)t;

    show_index(index);
    lv_screen_load(s_view);
}

static void item_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index >= 0 && index < s_count) {
        open_viewer(index);
    }
}

static void rebuild(void)
{
    lv_obj_clean(s_screen);

    const ui_theme_t *t = ui_theme();

    lv_obj_t *back = ui_button(s_screen, "首页", 84, 46, back_home_cb, NULL);
    lv_obj_set_pos(back, 16, 18);

    lv_obj_t *title = ui_label(s_screen, "图片", fonts_get(1), t->fg);
    lv_obj_set_pos(title, 116, 24);

    char count[48];
    snprintf(count, sizeof(count), "%d 张", s_count);
    lv_obj_t *badge = ui_label(s_screen, count, fonts_ui(), t->muted);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -110, 28);

    lv_obj_t *refresh = ui_button(s_screen, "刷新", 84, 46, refresh_cb, NULL);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -20, 20);

    lv_obj_t *list = lv_obj_create(s_screen);
    lv_obj_set_size(list, 448, 680);
    lv_obj_set_pos(list, 16, 92);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    if (!storage_ready()) {
        lv_obj_t *hint = ui_label(list, "SD 卡不可用", fonts_ui(), t->muted);
        lv_obj_set_style_pad_top(hint, 40, 0);
        return;
    }

    if (s_count == 0) {
        lv_obj_t *hint = ui_label(list,
                                  "没有图片\n\n把 .jpg / .png 放到 SD 卡\n/pictures 目录",
                                  fonts_ui(), t->muted);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(hint, 6, 0);
        lv_obj_set_style_pad_top(hint, 40, 0);
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
    s_count = storage_scan_media("pictures", IMAGE_EXTS, 5, s_files, MAX_IMAGES);
    rebuild();
    ui_toast("已刷新");
}

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    s_screen = NULL;
    if (s_decoder) {
        lv_image_cache_drop(NULL);
        esp_lv_decoder_deinit(s_decoder);
        s_decoder = NULL;
    }
}

void ui_gallery_show(void)
{
    close_view();

    /* 读图需要 SD DMA，先释放 BLE 主机 */
    if (ble_is_ready()) {
        ble_stop();
    }

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }

    /* 解码器只在图片页存活期间占用内部 RAM */
    if (!s_decoder && esp_lv_decoder_init(&s_decoder) != ESP_OK) {
        ESP_LOGW("gallery", "图片解码器初始化失败");
        s_decoder = NULL;
    }
    s_screen = ui_screen_new();
    lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    s_count = storage_scan_media("pictures", IMAGE_EXTS, 5, s_files, MAX_IMAGES);
    rebuild();

    lv_screen_load(s_screen);
}

void ui_gallery_open_index(int index)
{
    if (index >= 0 && index < s_count) {
        open_viewer(index);
    }
}
