#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode; /* wifi_auth_mode_t */
} net_wifi_ap_t;

void net_wifi_start(void);
bool net_wifi_is_connected(void);
bool net_wifi_has_credentials(void);
const char *net_wifi_get_ip(void);
void net_wifi_get_ssid(char *out, unsigned len);
esp_err_t net_wifi_set_credentials(const char *ssid, const char *pass);
void net_wifi_reconnect(void);
void net_wifi_disconnect(void);

/* 扫描附近 2.4G 热点（阻塞，最长约 5 秒，不要从 LVGL 线程调用） */
int net_wifi_scan(net_wifi_ap_t *out, int max);

/* 触发一次 SNTP 校时（已启动则重启轮询） */
void net_wifi_sync_time(void);

#ifdef __cplusplus
}
#endif
