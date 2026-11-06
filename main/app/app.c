#include "app.h"

#include "bsp/esp-bsp.h"
#include "debug_cmd.h"
#include "esp_log.h"
#include "fonts.h"
#include "nvs_flash.h"
#include "screenshot.h"
#include "storage/storage.h"
#include "ui/home.h"
#include "ui/reader.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "wifi.h"

static const char *TAG = "app";

void app_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(ret));
    }

    settings_init();
    reader_cfg_t *cfg = settings_get();
    ui_theme_set_index(cfg->theme);

    storage_init();
    net_wifi_start();

    bsp_display_lock(-1);

    if (fonts_init() != ESP_OK) {
        lv_obj_t *scr = ui_screen_new();
        lv_obj_t *label = ui_label(scr, "内置字体加载失败\n请重新执行 idf.py flash", &lv_font_montserrat_24,
                                   ui_theme()->fg);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_screen_load(scr);
        bsp_display_unlock();
        ESP_LOGE(TAG, "字体加载失败");
        return;
    }

    /* 图片解码器在进入图片页时才初始化，避免常驻占用内部 RAM */

    ui_reader_init();
    ui_home_show();
    debug_cmd_start();
    screenshot_dump_serial();

    bsp_display_unlock();

    ESP_LOGI(TAG, "启动完成");
}
