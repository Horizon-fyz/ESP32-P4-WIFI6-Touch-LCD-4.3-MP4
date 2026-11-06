#include <stdio.h>

#include "app/app.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

static const char *TAG = "main";

void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        /* 官方示例用 TRIPLE_PARTIAL，但 MIPI DSI 下拿不到 on_frame_buf_complete 回调
         * （启动日志有该警告），静态界面会留下切缓冲造成的残影。
         * 阅读器界面基本是静态的，改用单缓冲同步刷新，保证画面完整。
         * 注意：字形发糊是字体生成参数问题，与本项无关，见 README「字体」。 */
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    /* 内部 RAM 很紧张（WiFi/BLE/I2S 都要用），LVGL 任务栈放到 PSRAM */
    cfg.lv_adapter_cfg.stack_in_psram = true;

    if (!bsp_display_start_with_config(&cfg)) {
        ESP_LOGE(TAG, "屏幕初始化失败");
        return;
    }
    bsp_display_backlight_on();

    app_start();
}
