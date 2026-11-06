#include "ble.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble";

/* esp_hosted 1.4.7 没有实现这个符号，但 NimBLE 的 port_deinit 会引用它。
 * VHCI 传输在启动时已建立，关闭 NimBLE 主机时不需要拆传输，这里留空实现。 */
void ble_transport_ll_deinit(void)
{
}

#define MAX_DEVICES 32
#define DEVICE_NAME "MP4-Reader"

/* Nordic UART Service：手机用「BLE 调试助手」即可收发文本 */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00,
                     0x40, 0x6e);
static const ble_uuid128_t s_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00,
                     0x40, 0x6e);
static const ble_uuid128_t s_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00,
                     0x40, 0x6e);

static bool s_started;
static bool s_ready;
static bool s_scanning;
static uint8_t s_own_addr_type;
static uint16_t s_slave_conn;
static uint16_t s_master_conn;
static uint16_t s_tx_handle;
static uint16_t s_rx_handle;
static char s_last_text[64];
static SemaphoreHandle_t s_lock;

static ble_device_t s_devices[MAX_DEVICES];
static int s_device_count;
static uint32_t s_revision;

const char *ble_state_text(void)
{
    if (!s_started) {
        return "未启动";
    }
    if (!s_ready) {
        return "正在初始化…";
    }
    if (s_master_conn != BLE_HS_CONN_HANDLE_NONE) {
        return "已连接外部设备";
    }
    if (s_slave_conn != BLE_HS_CONN_HANDLE_NONE) {
        return "手机已连接";
    }
    if (s_scanning) {
        return "扫描中…";
    }
    return "就绪（可被手机连接）";
}

bool ble_is_ready(void)
{
    return s_ready;
}

bool ble_is_connected(void)
{
    return s_slave_conn != BLE_HS_CONN_HANDLE_NONE || s_master_conn != BLE_HS_CONN_HANDLE_NONE;
}

bool ble_phone_connected(void)
{
    return s_slave_conn != BLE_HS_CONN_HANDLE_NONE;
}

const char *ble_phone_last_text(void)
{
    return s_last_text;
}

uint32_t ble_scan_revision(void)
{
    return s_revision;
}

int ble_get_devices(ble_device_t *out, int max)
{
    if (!out || max <= 0 || !s_lock) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_device_count < max ? s_device_count : max;
    memcpy(out, s_devices, sizeof(ble_device_t) * (size_t)n);
    xSemaphoreGive(s_lock);
    return n;
}

static void add_device(const struct ble_gap_event *event)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return;
    }
    if (!fields.name || fields.name_len == 0) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].addr, event->disc.addr.val, 6) == 0) {
            s_devices[i].rssi = event->disc.rssi;
            xSemaphoreGive(s_lock);
            return;
        }
    }

    if (s_device_count >= MAX_DEVICES) {
        xSemaphoreGive(s_lock);
        return;
    }

    ble_device_t *d = &s_devices[s_device_count];
    memset(d, 0, sizeof(*d));
    memcpy(d->addr, event->disc.addr.val, 6);
    d->addr_type = event->disc.addr.type;
    d->rssi = event->disc.rssi;
    size_t n = fields.name_len < sizeof(d->name) - 1 ? fields.name_len : sizeof(d->name) - 1;
    memcpy(d->name, fields.name, n);
    d->name[n] = '\0';
    s_device_count++;
    s_revision++;

    ESP_LOGI(TAG, "发现设备 %s (%d dBm)", d->name, d->rssi);
    xSemaphoreGive(s_lock);
}

static int nus_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                         void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t buf[sizeof(s_last_text)];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    buf[len] = '\0';
    snprintf(s_last_text, sizeof(s_last_text), "%s", (const char *)buf);
    ESP_LOGI(TAG, "手机发来: %s", s_last_text);

    /* 原样回显，便于在手机端确认链路正常 */
    if (s_slave_conn != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
        if (om) {
            ble_gatts_notify_custom(s_slave_conn, s_tx_handle, om);
        }
    }
    return 0;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &s_rx_uuid.u,
                    .access_cb = nus_access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &s_rx_handle,
                },
                {
                    .uuid = &s_tx_uuid.u,
                    .access_cb = nus_access_cb,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_tx_handle,
                },
                {0},
            },
    },
    {0},
};

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status != 0) {
            start_advertising();
            break;
        }
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
            if (desc.role == BLE_GAP_ROLE_SLAVE) {
                s_slave_conn = event->connect.conn_handle;
                ESP_LOGI(TAG, "手机已连接");
            } else {
                s_master_conn = event->connect.conn_handle;
                ESP_LOGI(TAG, "已连接外部设备");
            }
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == s_slave_conn) {
            s_slave_conn = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        if (event->disconnect.conn.conn_handle == s_master_conn) {
            s_master_conn = BLE_HS_CONN_HANDLE_NONE;
        }
        ESP_LOGI(TAG, "BLE 断开，原因 0x%x", event->disconnect.reason);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    case BLE_GAP_EVENT_DISC:
        add_device(event);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = false;
        break;
    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    if (!s_ready || s_slave_conn != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "广播数据设置失败: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.name = (uint8_t *)DEVICE_NAME;
    rsp.name_len = strlen(DEVICE_NAME);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "开启广播失败: %d", rc);
    }
}

void ble_scan_start(void)
{
    if (!s_ready || s_scanning) {
        return;
    }

    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.passive = 0;
    params.filter_duplicates = 1;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc == 0) {
        s_scanning = true;
        ESP_LOGI(TAG, "开始扫描");
    } else if (rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "扫描失败: %d", rc);
    }
}

void ble_scan_stop(void)
{
    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }
}

esp_err_t ble_connect(const ble_device_t *dev)
{
    if (!s_ready || !dev) {
        return ESP_ERR_INVALID_STATE;
    }
    ble_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    memcpy(addr.val, dev->addr, 6);
    addr.type = dev->addr_type;

    int rc = ble_gap_connect(s_own_addr_type, &addr, 10000, NULL, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "连接失败: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void ble_disconnect(void)
{
    if (s_master_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_master_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (s_slave_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_slave_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    ble_svc_gap_device_name_set(DEVICE_NAME);

    s_ready = true;
    ESP_LOGI(TAG, "BLE 主机就绪，设备名 %s", DEVICE_NAME);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE 复位: %d", reason);
    s_ready = false;
}

static void host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task 启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_started = true;
    s_slave_conn = BLE_HS_CONN_HANDLE_NONE;
    s_master_conn = BLE_HS_CONN_HANDLE_NONE;

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s（C6 可能不支持 BLE）", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "nimble_port_init 完成");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void ble_stop(void)
{
    if (!s_started) {
        return;
    }
    s_started = false;

    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }
    if (s_slave_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_slave_conn, BLE_ERR_REM_USER_CONN_TERM);
        s_slave_conn = BLE_HS_CONN_HANDLE_NONE;
    }
    if (s_master_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_master_conn, BLE_ERR_REM_USER_CONN_TERM);
        s_master_conn = BLE_HS_CONN_HANDLE_NONE;
    }
    if (s_ready) {
        ble_gap_adv_stop();
    }

    nimble_port_stop();
    nimble_port_deinit();
    s_ready = false;

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_device_count = 0;
        s_revision++;
        xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG, "NimBLE 已释放");
}

