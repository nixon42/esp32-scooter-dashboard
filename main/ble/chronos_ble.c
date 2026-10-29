/**
 * @file chronos_ble.c
 * @brief Apache NimBLE GATT server for Chronos companion app implementation.
 */

#include "chronos_ble.h"
#include "chronos_packet.h"
#include "hud_state.h"

#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "CHRONOS_BLE";
static const char *DEVICE_NAME = "VarioHUD";
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

// Nordic UART Service 128-bit UUIDs (Little-Endian)
// 6e400001-b5a3-f393-e0a9-e50e24dcca9e
static const ble_uuid128_t s_uuid_service =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

// 6e400002-b5a3-f393-e0a9-e50e24dcca9e (RX)
static const ble_uuid128_t s_uuid_chr_rx =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

// 6e400003-b5a3-f393-e0a9-e50e24dcca9e (TX)
static const ble_uuid128_t s_uuid_chr_tx =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_chr_rx_val_handle;
static uint16_t s_chr_tx_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_client_subscribed = false;
static chronos_rx_buffer_t s_rx_buf;

static int chronos_gap_event(struct ble_gap_event *event, void *arg);

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    // 1. Primary Advertising Packet (Must be <= 31 bytes)
    // Flags (3B) + UUID128 (18B) + TX Power (3B) = 24 bytes <= 31 bytes
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    adv_fields.uuids128 = (ble_uuid128_t *)&s_uuid_service;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting adv fields: rc=%d", rc);
        return;
    }

    // 2. Scan Response Packet (Must be <= 31 bytes)
    // Device Name "VarioHUD" (10B) <= 31 bytes
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (uint8_t *)DEVICE_NAME;
    rsp_fields.name_len = strlen(DEVICE_NAME);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting rsp fields: rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 48; // 30 ms fast reconnect
    adv_params.itvl_max = 96; // 60 ms fast reconnect

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, chronos_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertising: rc=%d", rc);
        return;
    }

    char mac_str[18] = {0};
    chronos_ble_get_mac_str(mac_str, sizeof(mac_str));
    ESP_LOGI(TAG, "BLE advertising started as '%s' (MAC: %s)", DEVICE_NAME, mac_str);
}

static int chronos_gatt_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (attr_handle == s_chr_rx_val_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            s_conn_handle = conn_handle;
            s_client_subscribed = true;
            uint8_t data[CHRONOS_MAX_PACKET_LEN];
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > sizeof(data)) len = sizeof(data);

            os_mbuf_copydata(ctxt->om, 0, len, data);
            chronos_packet_feed(&s_rx_buf, data, len);
            return 0;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_service.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_uuid_chr_rx.u,
                .access_cb = chronos_gatt_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_chr_rx_val_handle,
            },
            {
                .uuid = &s_uuid_chr_tx.u,
                .access_cb = chronos_gatt_chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_chr_tx_val_handle,
            },
            { 0 }
        }
    },
    { 0 }
};

static int chronos_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE connection established, status=%d", event->connect.status);
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_client_subscribed = true;
                hud_state_set_ble_connected(true);
            } else {
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_client_subscribed = false;
            hud_state_set_ble_disconnected();
            hud_state_sync_clock_to_nvs();
            start_advertising();
            break;


        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "BLE subscribe event: handle=%d, cur_notify=%d",
                     event->subscribe.attr_handle, event->subscribe.cur_notify);
            if (event->subscribe.attr_handle == s_chr_tx_val_handle && event->subscribe.cur_notify) {
                s_client_subscribed = true;

                // Transmit Chronos device info handshake packet (non-blocking)
                uint8_t packet[32];
                size_t len = chronos_get_info_packet(packet);
                chronos_ble_send_notify(packet, len);
            } else {
                s_client_subscribed = false;
            }
            break;

        default:
            break;
    }
    return 0;
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced.");
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error ensuring addr: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type: %d", rc);
        return;
    }
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started.");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t chronos_ble_init(void)
{
    ESP_LOGI(TAG, "Initializing Chronos NimBLE stack...");
    chronos_packet_init(&s_rx_buf);

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init nimble port: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set GAP device name: %d", rc);
    }

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT svcs: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT svcs: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "Chronos NimBLE stack initialized.");
    return ESP_OK;
}

esp_err_t chronos_ble_send_notify(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notify");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_chr_tx_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send notify: rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool chronos_ble_is_connected(void)
{
    return (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
}

esp_err_t chronos_ble_get_mac_str(char *buf, size_t max_len)
{
    if (!buf || max_len < 18) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BT);
    if (err == ESP_OK) {
        snprintf(buf, max_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return ESP_OK;
    }
    return err;
}

esp_err_t chronos_ble_request_sync(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t packet[32];
    size_t len;

    // 1. Sync packet (0xFE 0x23) -> Requests Time, Weather forecast, Alarms from Chronos app
    len = chronos_get_sync_packet(packet);
    esp_err_t err = chronos_ble_send_notify(packet, len);
    if (err != ESP_OK) {
        return err;
    }

    // Small delay between packets to prevent BLE buffer congestion
    vTaskDelay(pdMS_TO_TICKS(35));

    // 2. Phone battery request (0xFE 0x91 0x80 0x01) -> Requests phone battery % & charging status
    len = chronos_get_request_phone_battery_packet(packet);
    return chronos_ble_send_notify(packet, len);
}
