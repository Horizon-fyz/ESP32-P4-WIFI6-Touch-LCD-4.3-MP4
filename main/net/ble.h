#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    char name[32];
    int8_t rssi;
} ble_device_t;

/* 启动 BLE 主机（首次进入蓝牙页时调用；失败不影响其它功能） */
esp_err_t ble_start(void);
/* 离开蓝牙页时释放 NimBLE 占用的内部/DMA RAM，下次进入会重新初始化 */
void ble_stop(void);
bool ble_is_ready(void);

/* 中心模式：扫描附近 BLE 设备 */
void ble_scan_start(void);
void ble_scan_stop(void);
int ble_get_devices(ble_device_t *out, int max);
/* 每次扫描结果变化时自增，用于 UI 轮询 */
uint32_t ble_scan_revision(void);

esp_err_t ble_connect(const ble_device_t *dev);
void ble_disconnect(void);
bool ble_is_connected(void);

/* 从机模式：手机连接本机后的状态与收到的文本（UTF-8） */
bool ble_phone_connected(void);
const char *ble_phone_last_text(void);

const char *ble_state_text(void);

#ifdef __cplusplus
}
#endif
