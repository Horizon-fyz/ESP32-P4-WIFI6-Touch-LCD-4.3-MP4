#include "fonts.h"

#include <stdio.h>

#include "esp_log.h"
#include "font/binfont_loader/lv_binfont_loader.h"
#include "fonts_manifest.h"

static const char *TAG = "fonts";

/*
 * 中文字体按「字形位图大小」拆成多个 .bin 包，再用 lv_font_t.fallback 串联。
 * 原因：lv_font_fmt_txt_glyph_dsc_t.bitmap_index 只有 20 位，单个字体的
 * 解码后位图不能超过 1MB，否则索引回绕、后半段字形全部错位。
 * 包清单由 tools/gen_fonts.py 自动生成到 fonts_manifest.h。
 */
static lv_font_t *s_fonts[FONT_SIZE_COUNT];
static int s_current = -1;

static lv_font_t *load_size(int index)
{
    const font_pack_t *pack = &g_font_packs[index];

    /* 从最后一包往前加载，让 part0 成为入口，依次 fallback 到下一包 */
    lv_font_t *next = &lv_font_montserrat_20;
    for (int i = pack->part_count - 1; i >= 0; i--) {
        size_t bytes = (size_t)(pack->end[i] - pack->start[i]);
        lv_font_t *font = lv_binfont_create_from_buffer((void *)pack->start[i], (uint32_t)bytes);
        if (!font) {
            ESP_LOGE(TAG, "字体包解析失败: %dpx 第 %d 包 (%u 字节)",
                     pack->size_px, i, (unsigned)bytes);
            return NULL;
        }
        font->fallback = next;
        next = font;
    }
    return next;
}

static lv_font_t *ensure_font(int index)
{
    if (index < 0 || index >= FONT_SIZE_COUNT) {
        index = 0;
    }
    if (!s_fonts[index]) {
        s_fonts[index] = load_size(index);
    }
    return s_fonts[index];
}

esp_err_t fonts_init(void)
{
    /* 22px（界面）与 26px（默认正文）开机加载，其余字号首次使用时再解析 */
    if (!ensure_font(0) || !ensure_font(1)) {
        return ESP_FAIL;
    }
    s_current = 0;
    ESP_LOGI(TAG, "界面字体与默认正文字体已加载（共 %d 档）", FONT_SIZE_COUNT);
    return ESP_OK;
}

const lv_font_t *fonts_ui(void)
{
    return s_fonts[0] ? s_fonts[0] : &lv_font_montserrat_24;
}

int fonts_count(void)
{
    return FONT_SIZE_COUNT;
}

int fonts_size_px(int index)
{
    if (index < 0 || index >= FONT_SIZE_COUNT) {
        index = 0;
    }
    return g_font_packs[index].size_px;
}

const lv_font_t *fonts_get(int index)
{
    lv_font_t *font = ensure_font(index);
    return font ? font : &lv_font_montserrat_24;
}

const lv_font_t *fonts_select_reader(int index)
{
    if (index < 0 || index >= FONT_SIZE_COUNT) {
        index = 0;
    }
    s_current = index;
    lv_font_t *font = ensure_font(index);
    return font ? font : &lv_font_montserrat_24;
}

const lv_font_t *fonts_current_reader(void)
{
    if (s_current < 0) {
        s_current = 0;
    }
    return s_fonts[s_current] ? s_fonts[s_current] : &lv_font_montserrat_24;
}
