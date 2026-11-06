#include "theme.h"

#include <stdint.h>

/* 0xRRGGBB 形式的颜色表 */
static const uint32_t s_colors[UI_THEME_COUNT][6] = {
    /* bg, panel, fg, muted, accent, line */
    {0xF6F1E7, 0xFFFFFF, 0x2B2B2B, 0x8A8378, 0x8B5E3C, 0xDDD5C7}, /* 纸白 */
    {0x101214, 0x1A1D21, 0xC7C9CC, 0x7A8087, 0x6FA8DC, 0x2A2E33}, /* 夜间 */
    {0xDCE9D4, 0xEAF3E4, 0x28351F, 0x6B7A63, 0x3E6B48, 0xC3D4B9}, /* 护眼 */
};

static int s_index = 0;
static ui_theme_t s_current;
static bool s_ready = false;

static void refresh(void)
{
    const uint32_t *c = s_colors[s_index];
    s_current.bg = lv_color_hex(c[0]);
    s_current.panel = lv_color_hex(c[1]);
    s_current.fg = lv_color_hex(c[2]);
    s_current.muted = lv_color_hex(c[3]);
    s_current.accent = lv_color_hex(c[4]);
    s_current.line = lv_color_hex(c[5]);
    s_ready = true;
}

const ui_theme_t *ui_theme(void)
{
    if (!s_ready) {
        refresh();
    }
    return &s_current;
}

int ui_theme_index(void)
{
    return s_index;
}

void ui_theme_set_index(int index)
{
    if (index < 0 || index >= UI_THEME_COUNT) {
        index = 0;
    }
    s_index = index;
    refresh();
}

const char *ui_theme_name(int index)
{
    static const char *names[UI_THEME_COUNT] = {"纸白", "夜间", "护眼"};
    if (index < 0 || index >= UI_THEME_COUNT) {
        return names[0];
    }
    return names[index];
}
