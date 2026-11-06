#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "storage/storage.h"
#include "web_server.h"

static const char *TAG = "wifi";

#define WIFI_NVS_NAMESPACE "wifi"
#define RETRY_MAX 8
#define SCAN_MAX 32

static bool s_connected;
static bool s_has_credentials;
static char s_ip[32] = "0.0.0.0";
static char s_ssid[33];
static char s_pass[65];
static int s_retry;
static EventGroupHandle_t s_events;
static bool s_started;
static bool s_sntp_started;
static bool s_manual_disconnect;

void net_wifi_sync_time(void)
{
    if (!s_sntp_started) {
        s_sntp_started = true;
        setenv("TZ", "CST-8", 1);
        tzset();
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP 已启动");
    } else {
        esp_sntp_restart();
        ESP_LOGI(TAG, "SNTP 重新校时");
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_has_credentials) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
        if (s_manual_disconnect) {
            return;
        }
        if (s_retry < RETRY_MAX) {
            s_retry++;
            ESP_LOGW(TAG, "连接断开，重试 %d/%d", s_retry, RETRY_MAX);
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi 连接失败");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_retry = 0;
        ESP_LOGI(TAG, "已连接 %s，IP %s", s_ssid, s_ip);
        net_wifi_sync_time();
        web_server_start();
    }
}

static bool load_credentials(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_ssid);
    s_ssid[0] = '\0';
    s_pass[0] = '\0';

    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, "ssid", s_ssid, &len) != ESP_OK) {
            s_ssid[0] = '\0';
        }
        len = sizeof(s_pass);
        if (nvs_get_str(h, "pass", s_pass, &len) != ESP_OK) {
            s_pass[0] = '\0';
        }
        nvs_close(h);
    }

    if (s_ssid[0] == '\0' && storage_ready()) {
        FILE *f = fopen("/sdcard/wifi.txt", "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                char *p = line;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                size_t n = strlen(p);
                while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) {
                    p[--n] = '\0';
                }
                if (strncasecmp(p, "ssid=", 5) == 0) {
                    snprintf(s_ssid, sizeof(s_ssid), "%s", p + 5);
                } else if (strncasecmp(p, "pass=", 5) == 0 || strncasecmp(p, "password=", 9) == 0) {
                    const char *v = strchr(p, '=') + 1;
                    snprintf(s_pass, sizeof(s_pass), "%s", v);
                }
            }
            fclose(f);
            if (s_ssid[0]) {
                ESP_LOGI(TAG, "从 wifi.txt 读取到 SSID: %s", s_ssid);
            }
        }
    }

    s_has_credentials = s_ssid[0] != '\0';
    return s_has_credentials;
}

static void connect_now(void)
{
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.sta.ssid, s_ssid, strnlen(s_ssid, sizeof(cfg.sta.ssid) - 1));
    memcpy(cfg.sta.password, s_pass, strnlen(s_pass, sizeof(cfg.sta.password) - 1));
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

void net_wifi_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif 初始化失败: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "事件循环创建失败: %s", esp_err_to_name(err));
        return;
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "mp4-reader");

    s_events = xEventGroupCreate();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 初始化失败（C6 协处理器可能未就绪）: %s", esp_err_to_name(err));
        return;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置 STA 模式失败: %s", esp_err_to_name(err));
        return;
    }

    /* 即使没有凭据也要启动驱动：设置页需要扫描附近热点 */
    load_credentials();
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 启动失败: %s", esp_err_to_name(err));
    }
}

bool net_wifi_is_connected(void)
{
    return s_connected;
}

bool net_wifi_has_credentials(void)
{
    return s_has_credentials;
}

const char *net_wifi_get_ip(void)
{
    return s_ip;
}

void net_wifi_get_ssid(char *out, unsigned len)
{
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%s", s_ssid);
}

esp_err_t net_wifi_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", pass ? pass : "");
    s_has_credentials = true;
    s_manual_disconnect = false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_set_str(h, "ssid", s_ssid);
    nvs_set_str(h, "pass", s_pass);
    nvs_commit(h);
    nvs_close(h);

    net_wifi_reconnect();
    return ESP_OK;
}

void net_wifi_disconnect(void)
{
    s_manual_disconnect = true;
    s_has_credentials = false;
    s_connected = false;
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    snprintf(s_ip, sizeof(s_ip), "0.0.0.0");

    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
        nvs_commit(h);
        nvs_close(h);
    }
    if (s_started) {
        esp_wifi_disconnect();
    }
    ESP_LOGI(TAG, "已清除 WiFi 配置");
}

static void reconnect_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    if (!s_started) {
        net_wifi_start();
    } else {
        s_manual_disconnect = false;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(300));
        connect_now();
    }
    vTaskDelete(NULL);
}

void net_wifi_reconnect(void)
{
    if (!s_has_credentials) {
        return;
    }
    s_retry = 0;
    xTaskCreate(reconnect_task, "net_wifi_reconnect", 4096, NULL, 4, NULL);
}

static int cmp_ap(const void *a, const void *b)
{
    const net_wifi_ap_t *x = a;
    const net_wifi_ap_t *y = b;
    return (int)y->rssi - (int)x->rssi;
}

int net_wifi_scan(net_wifi_ap_t *out, int max)
{
    if (!s_started || !out || max <= 0) {
        return 0;
    }

    wifi_scan_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.show_hidden = false;
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        return 0;
    }

    uint16_t num = SCAN_MAX;
    wifi_ap_record_t *recs = calloc(SCAN_MAX, sizeof(wifi_ap_record_t));
    if (!recs) {
        return 0;
    }
    if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
        free(recs);
        return 0;
    }

    int count = 0;
    for (uint16_t i = 0; i < num && count < max; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(out[j].ssid, (const char *)recs[i].ssid) == 0) {
                if (recs[i].rssi > out[j].rssi) {
                    out[j].rssi = recs[i].rssi;
                    out[j].authmode = recs[i].authmode;
                }
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        memset(&out[count], 0, sizeof(out[count]));
        snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", (const char *)recs[i].ssid);
        out[count].rssi = recs[i].rssi;
        out[count].authmode = recs[i].authmode;
        count++;
    }
    free(recs);

    if (count > 1) {
        qsort(out, (size_t)count, sizeof(out[0]), cmp_ap);
    }
    return count;
}
