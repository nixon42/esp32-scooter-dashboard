/**
 * @file web_portal.c
 * @brief Wi-Fi SoftAP and embedded Web Configuration & OTA Portal implementation.
 */

#include "web_portal.h"
#include "index_html.h"
#include "hud_state.h"
#include "pcd8544.h"
#include "voltmeter.h"
#include "speedometer.h"
#include "ldr_sensor.h"
#include "buzzer.h"
#include "chronos_ble.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "WEB_PORTAL";
static httpd_handle_t s_server = NULL;
static int64_t s_last_http_activity_us = 0;
static bool    s_wifi_stopped = false;

void web_portal_activity_ping(void)
{
    s_last_http_activity_us = esp_timer_get_time();
}

bool web_portal_is_running(void)
{
    return (s_server != NULL && !s_wifi_stopped);
}

void web_portal_check_auto_sleep(void)
{
    if (s_wifi_stopped || !s_server) return;

    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    if (snap.wifi_ap_timeout_min == 0) {
        return; // 0 = Always ON (tidak pernah mati)
    }

    int64_t now_us = esp_timer_get_time();
    int64_t timeout_us = (int64_t)snap.wifi_ap_timeout_min * 60ULL * 1000000ULL;

    if (s_last_http_activity_us == 0) {
        s_last_http_activity_us = now_us;
    }

    if ((now_us - s_last_http_activity_us) >= timeout_us) {
        ESP_LOGI(TAG, "Wi-Fi SoftAP auto-sleep triggered (%d min inactive). Shutting down Wi-Fi to preserve RF & power.", snap.wifi_ap_timeout_min);
        web_portal_stop();
    }
}

static void restart_timer_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Rebooting system in 1.5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    web_portal_activity_ping();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    web_portal_activity_ping();
    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "speed", snap.speed_kmh);
    cJSON_AddStringToObject(root, "speed_src",
        snap.speed_source == SPEED_SOURCE_VSS ? "VSS Hardware" :
        (snap.speed_source == SPEED_SOURCE_BLE_GPS ? "BLE GPS" : "Standby"));

    cJSON_AddBoolToObject(root, "battery_en", snap.battery_enabled);
    cJSON_AddBoolToObject(root, "battery_valid", snap.battery_voltage >= 5.0f);
    cJSON_AddNumberToObject(root, "voltage", snap.battery_voltage);

    cJSON_AddBoolToObject(root, "dht_en", snap.dht_enabled);
    cJSON_AddBoolToObject(root, "dht_valid", snap.dht_valid);
    cJSON_AddNumberToObject(root, "temp", snap.temperature_c);
    cJSON_AddNumberToObject(root, "hum", snap.humidity_pct);

    cJSON_AddBoolToObject(root, "ble_connected", snap.ble_connected);
    char ble_mac[18] = {0};
    chronos_ble_get_mac_str(ble_mac, sizeof(ble_mac));
    cJSON_AddStringToObject(root, "ble_mac", ble_mac);
    cJSON_AddNumberToObject(root, "phone_battery", snap.phone_battery_pct);
    cJSON_AddBoolToObject(root, "phone_charging", snap.phone_charging);
    cJSON_AddBoolToObject(root, "music_active", snap.music_active);
    cJSON_AddStringToObject(root, "music_title", snap.music_title);
    cJSON_AddStringToObject(root, "music_artist", snap.music_artist);

    cJSON_AddBoolToObject(root, "nav_active", snap.nav_active);
    cJSON_AddStringToObject(root, "nav_title", snap.nav_title);
    cJSON_AddStringToObject(root, "nav_dist", snap.nav_distance);

    cJSON_AddBoolToObject(root, "weather_valid", snap.weather_valid);
    cJSON_AddStringToObject(root, "weather_city", snap.weather_city);
    cJSON_AddStringToObject(root, "weather_region", snap.weather_region);
    cJSON_AddNumberToObject(root, "weather_temp", snap.weather_current_temp);
    cJSON_AddNumberToObject(root, "weather_icon", snap.weather_current_icon);

    chronos_hourly_forecast_t fc;
    if (hud_state_get_forecast_1h(&snap, &fc)) {
        cJSON_AddBoolToObject(root, "fc_valid", true);
        cJSON_AddNumberToObject(root, "fc_hour", fc.hour);
        cJSON_AddNumberToObject(root, "fc_temp", fc.temp);
        cJSON_AddNumberToObject(root, "fc_icon", fc.icon);
    } else {
        cJSON_AddBoolToObject(root, "fc_valid", false);
    }

    cJSON_AddNumberToObject(root, "clock_style", (int)snap.clock_style);
    cJSON_AddBoolToObject(root, "time_synced", snap.time_synced);
    char time_str[32] = "--:--:--";
    if (snap.time_synced) {
        time_t now;
        time(&now);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    }
    cJSON_AddStringToObject(root, "device_time", time_str);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime", (uint32_t)(esp_timer_get_time() / 1000000ULL));


    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    esp_err_t res = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

    free(json_str);
    cJSON_Delete(root);
    return res;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    web_portal_activity_ping();
    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "battery_enabled", snap.battery_enabled);
    cJSON_AddNumberToObject(root, "battery_ratio", snap.battery_ratio);
    cJSON_AddBoolToObject(root, "vss_enabled", snap.vss_enabled);
    cJSON_AddNumberToObject(root, "pulses_per_meter", snap.pulses_per_meter);
    cJSON_AddBoolToObject(root, "dht_enabled", snap.dht_enabled);
    cJSON_AddNumberToObject(root, "dht_type", (int)snap.dht_type);
    cJSON_AddBoolToObject(root, "ldr_enabled", snap.ldr_enabled);
    cJSON_AddNumberToObject(root, "bl_mode", (int)snap.bl_mode);
    cJSON_AddNumberToObject(root, "bl_brightness", snap.bl_brightness);
    cJSON_AddBoolToObject(root, "bl_inverted", snap.bl_inverted);
    cJSON_AddNumberToObject(root, "lcd_contrast", snap.lcd_contrast);
    cJSON_AddNumberToObject(root, "clock_style", (int)snap.clock_style);
    cJSON_AddNumberToObject(root, "boot_logo", (int)snap.boot_logo);
    cJSON_AddBoolToObject(root, "media_notif_enabled", snap.media_notif_enabled);
    cJSON_AddNumberToObject(root, "ui_cycle", snap.ui_cycle_sec);
    cJSON_AddNumberToObject(root, "nav_cycle", snap.nav_cycle_sec);
    cJSON_AddNumberToObject(root, "notif_timeout", snap.notif_timeout_sec);
    cJSON_AddNumberToObject(root, "call_timeout", snap.call_timeout_sec);
    cJSON_AddBoolToObject(root, "alert_cue", snap.alert_cue_enabled);
    cJSON_AddBoolToObject(root, "buzzer_enabled", snap.buzzer_enabled);
    cJSON_AddNumberToObject(root, "buzzer_pin", snap.buzzer_pin);
    cJSON_AddNumberToObject(root, "buzzer_type", (int)snap.buzzer_type);
    cJSON_AddNumberToObject(root, "buzzer_volume", snap.buzzer_volume);
    cJSON_AddNumberToObject(root, "buzzer_profile", (int)snap.buzzer_profile);
    cJSON_AddBoolToObject(root, "buzzer_boot_en", snap.buzzer_boot_en);
    cJSON_AddBoolToObject(root, "buzzer_notif_en", snap.buzzer_notif_en);
    cJSON_AddBoolToObject(root, "buzzer_call_en", snap.buzzer_call_en);
    cJSON_AddBoolToObject(root, "buzzer_nav_en", snap.buzzer_nav_en);
    cJSON_AddNumberToObject(root, "wifi_ap_timeout", snap.wifi_ap_timeout_min);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    esp_err_t res = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

    free(json_str);
    cJSON_Delete(root);
    return res;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int total = req->content_len;
    if (total <= 0 || total >= sizeof(buf)) {
        ESP_LOGE(TAG, "Invalid config content length: %d", total);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    int cur = 0;
    while (cur < total) {
        int r = httpd_req_recv(req, buf + cur, total - cur);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed receiving config body (cur=%d, total=%d)", cur, total);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur += r;
    }
    buf[cur] = '\0';
    ESP_LOGI(TAG, "Config received from web: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed parsing JSON payload");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    cJSON *item = NULL;
    if ((item = cJSON_GetObjectItem(root, "battery_enabled")) != NULL) {
        snap.battery_enabled = cJSON_IsTrue(item);
        voltmeter_set_enabled(snap.battery_enabled);
    }
    if ((item = cJSON_GetObjectItem(root, "battery_ratio")) != NULL && cJSON_IsNumber(item)) {
        snap.battery_ratio = (float)item->valuedouble;
        voltmeter_set_ratio(snap.battery_ratio);
    }
    if ((item = cJSON_GetObjectItem(root, "vss_enabled")) != NULL) {
        snap.vss_enabled = cJSON_IsTrue(item);
        speedometer_set_enabled(snap.vss_enabled);
    }
    if ((item = cJSON_GetObjectItem(root, "pulses_per_meter")) != NULL && cJSON_IsNumber(item)) {
        snap.pulses_per_meter = (float)item->valuedouble;
        speedometer_set_pulses_per_meter(snap.pulses_per_meter);
    }
    if ((item = cJSON_GetObjectItem(root, "dht_enabled")) != NULL) {
        snap.dht_enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "dht_type")) != NULL && cJSON_IsNumber(item)) {
        snap.dht_type = (dht_type_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(root, "ldr_enabled")) != NULL) {
        snap.ldr_enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "bl_mode")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint <= BL_MODE_AUTO_CLOCK) {
            snap.bl_mode = (backlight_mode_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "bl_brightness")) != NULL && cJSON_IsNumber(item)) {
        snap.bl_brightness = (uint8_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(root, "bl_inverted")) != NULL) {
        snap.bl_inverted = cJSON_IsTrue(item);
        pcd8544_set_backlight_inverted(snap.bl_inverted);
    }
    if ((item = cJSON_GetObjectItem(root, "lcd_contrast")) != NULL && cJSON_IsNumber(item)) {
        uint8_t contrast = (uint8_t)item->valueint;
        if (contrast >= 0x38 && contrast <= 0x48) {
            snap.lcd_contrast = contrast;
            pcd8544_set_contrast(contrast);
        }
    }
    if ((item = cJSON_GetObjectItem(root, "clock_style")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint < CLOCK_STYLE_MAX) {
            snap.clock_style = (clock_style_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "boot_logo")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint < BOOT_LOGO_MAX) {
            snap.boot_logo = (boot_logo_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "media_notif_enabled")) != NULL) {
        snap.media_notif_enabled = cJSON_IsTrue(item);
        if (!snap.media_notif_enabled) {
            snap.music_active = false;
            snap.music_title[0] = '\0';
            snap.music_artist[0] = '\0';
        }
    }
    if ((item = cJSON_GetObjectItem(root, "ui_cycle")) != NULL && cJSON_IsNumber(item)) {
        snap.ui_cycle_sec = (uint8_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(root, "nav_cycle")) != NULL && cJSON_IsNumber(item)) {
        snap.nav_cycle_sec = (uint8_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(root, "notif_timeout")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 3 && item->valueint <= 30) {
            snap.notif_timeout_sec = (uint8_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "call_timeout")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint <= 60) {
            snap.call_timeout_sec = (uint8_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "alert_cue")) != NULL) {
        snap.alert_cue_enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_enabled")) != NULL) {
        snap.buzzer_enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_pin")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint == 20 || item->valueint == 21 || item->valueint == 9) {
            snap.buzzer_pin = (uint8_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_type")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint <= 1) {
            snap.buzzer_type = (uint8_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_volume")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 10 && item->valueint <= 100) {
            snap.buzzer_volume = (uint8_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_profile")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint < BUZZER_PROFILE_MAX) {
            snap.buzzer_profile = (buzzer_profile_t)item->valueint;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_boot_en")) != NULL) {
        snap.buzzer_boot_en = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_notif_en")) != NULL) {
        snap.buzzer_notif_en = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_call_en")) != NULL) {
        snap.buzzer_call_en = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "buzzer_nav_en")) != NULL) {
        snap.buzzer_nav_en = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "wifi_ap_timeout")) != NULL && cJSON_IsNumber(item)) {
        if (item->valueint >= 0 && item->valueint <= 60) {
            snap.wifi_ap_timeout_min = (uint8_t)item->valueint;
        }
    }

    web_portal_activity_ping();

    // Trigger physical notification popup on Nokia 5110 LCD screen
    snap.notification_active = true;
    snap.notif_app_id = 0x02; // General notification (auto-dismiss after ~5s)
    strncpy(snap.notif_title, "CONFIG", sizeof(snap.notif_title) - 1);
    strncpy(snap.notif_message, "SAVED TO NVS OK", sizeof(snap.notif_message) - 1);
    snap.notif_timestamp = (uint32_t)(esp_timer_get_time() / 1000ULL);

    hud_state_update(&snap);
    hud_state_save_to_nvs();
    buzzer_configure(snap.buzzer_pin, (buzzer_type_t)snap.buzzer_type, snap.buzzer_enabled, snap.buzzer_volume, snap.buzzer_profile);
    buzzer_set_event_enables(snap.buzzer_boot_en, snap.buzzer_notif_en, snap.buzzer_call_en, snap.buzzer_nav_en);
    ldr_sensor_poll_and_update_backlight();

    cJSON_Delete(root);

    ESP_LOGI(TAG, "HUD configuration saved to NVS and notified on LCD.");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Settings saved to NVS\"}");
}

static esp_err_t buzzer_test_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Web buzzer test triggered via /api/buzzer/test");
    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    uint8_t vol = snap.buzzer_volume;
    buzzer_profile_t prof = snap.buzzer_profile;
    buzzer_tone_t tone = BUZZER_TONE_NOTIF;

    char buf[128];
    int total = req->content_len;
    if (total > 0 && total < sizeof(buf)) {
        int r = httpd_req_recv(req, buf, total);
        if (r > 0) {
            buf[r] = '\0';
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *item = cJSON_GetObjectItem(root, "volume");
                if (item && cJSON_IsNumber(item) && item->valueint >= 10 && item->valueint <= 100) {
                    vol = (uint8_t)item->valueint;
                }
                item = cJSON_GetObjectItem(root, "profile");
                if (item && cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint < BUZZER_PROFILE_MAX) {
                    prof = (buzzer_profile_t)item->valueint;
                }
                item = cJSON_GetObjectItem(root, "tone");
                if (item && cJSON_IsNumber(item)) {
                    tone = (buzzer_tone_t)item->valueint;
                }
                cJSON_Delete(root);
            }
        }
    }

    buzzer_configure(snap.buzzer_pin, (buzzer_type_t)snap.buzzer_type, true, vol, prof);
    buzzer_set_event_enables(true, true, true, true);
    buzzer_play(tone);
    buzzer_set_event_enables(snap.buzzer_boot_en, snap.buzzer_notif_en, snap.buzzer_call_en, snap.buzzer_nav_en);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Buzzer tested successfully\"}");
}

static esp_err_t restart_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Web reboot requested via /api/restart");
    xTaskCreate(restart_timer_task, "restart_task", 2048, NULL, 5, NULL);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Rebooting ESP32...\"}");
}

static esp_err_t time_post_handler(httpd_req_t *req)
{
    web_portal_activity_ping();
    char buf[128];
    int remaining = req->content_len;
    if (remaining <= 0 || remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }
    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *epoch_item = cJSON_GetObjectItem(root, "epoch");
    if (epoch_item && cJSON_IsNumber(epoch_item)) {
        time_t epoch = (time_t)epoch_item->valuedouble;
        if (epoch > 1700000000) {
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            hud_state_set_time_synced(true);
            hud_state_save_clock_time(epoch, true);
            ESP_LOGI(TAG, "Clock synchronized via Web Portal: epoch=%ld", (long)epoch);
        }
    }

    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Time synced successfully\"}");
}

static esp_err_t sync_post_handler(httpd_req_t *req)
{
    web_portal_activity_ping();
    if (!chronos_ble_is_connected()) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"BLE belum terhubung ke aplikasi Chronos di HP\"}");
    }
    esp_err_t err = chronos_ble_request_sync();
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Force sync requested via Web Portal.");
        return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Permintaan sync cuaca & jam berhasil dikirim ke HP\"}");
    } else {
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Gagal mengirim paket sync BLE\"}");
    }
}


static esp_err_t ota_update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Starting Web OTA update. Content length: %d", req->content_len);

    if (req->content_len <= 0) {
        ESP_LOGE(TAG, "OTA content length invalid: %d", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File firmware kosong atau ukuran tidak valid");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found to flash");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Partisi OTA tidak tersedia");
        return ESP_FAIL;
    }

    if ((size_t)req->content_len > update_partition->size) {
        ESP_LOGE(TAG, "OTA payload size (%d) exceeds partition size (%u)",
                 req->content_len, (unsigned int)update_partition->size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ukuran firmware melebihi kapasitas partisi OTA");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Flashing to partition: %s (subtype %d, offset 0x%08lx)",
             update_partition->label, update_partition->subtype, update_partition->address);

    // Notify LCD that Web OTA flashing has started
    hud_state_t ota_snap;
    hud_state_get_snapshot(&ota_snap);
    ota_snap.ota_in_progress = true;
    ota_snap.ota_progress_pct = 0;
    hud_state_update(&ota_snap);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        ota_snap.ota_in_progress = false;
        hud_state_update(&ota_snap);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gagal menginisialisasi partisi OTA");
        return ESP_FAIL;
    }

    char ota_buf[1024];
    int remaining = req->content_len;
    int total_size = req->content_len;
    bool is_first_chunk = true;
    int timeout_count = 0;
    uint8_t last_pct = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, ota_buf, (remaining < sizeof(ota_buf)) ? remaining : sizeof(ota_buf));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                timeout_count++;
                if (timeout_count > 10) { // 10 x 5 detik = 50s timeout
                    ESP_LOGE(TAG, "OTA upload stalled: socket timeout limit reached");
                    esp_ota_abort(ota_handle);
                    ota_snap.ota_in_progress = false;
                    hud_state_update(&ota_snap);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Koneksi upload firmware terputus (Timeout)");
                    return ESP_FAIL;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "OTA receive error: %d", recv_len);
            esp_ota_abort(ota_handle);
            ota_snap.ota_in_progress = false;
            hud_state_update(&ota_snap);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Transfer data firmware gagal");
            return ESP_FAIL;
        }
        timeout_count = 0;

        // Validasi byte ajaib header ESP image (0xE9) pada chunk pertama
        if (is_first_chunk) {
            if ((uint8_t)ota_buf[0] != 0xE9) {
                ESP_LOGE(TAG, "OTA binary validation failed: magic byte 0x%02x != 0xE9", (uint8_t)ota_buf[0]);
                esp_ota_abort(ota_handle);
                ota_snap.ota_in_progress = false;
                hud_state_update(&ota_snap);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File bukan firmware biner ESP32 yang valid (Magic byte != 0xE9)");
                return ESP_FAIL;
            }
            is_first_chunk = false;
        }

        err = esp_ota_write(ota_handle, ota_buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            ota_snap.ota_in_progress = false;
            hud_state_update(&ota_snap);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gagal menulis firmware ke flash");
            return ESP_FAIL;
        }

        remaining -= recv_len;

        // Update progress bar LCD secara berkala
        int written = total_size - remaining;
        uint8_t cur_pct = (uint8_t)(((uint32_t)written * 100) / total_size);
        if (cur_pct >= last_pct + 4 || remaining == 0) {
            last_pct = cur_pct;
            ota_snap.ota_progress_pct = cur_pct;
            hud_state_update(&ota_snap);
        }

        // Berikan waktu sejenak (yield) untuk FreeRTOS task & watchdog
        if ((written % 32768) < sizeof(ota_buf)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end validation failed: %s", esp_err_to_name(err));
        ota_snap.ota_in_progress = false;
        hud_state_update(&ota_snap);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validasi checksum citra firmware gagal");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_snap.ota_in_progress = false;
        hud_state_update(&ota_snap);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gagal mengaktifkan partisi boot baru");
        return ESP_FAIL;
    }

    // Tampilkan 100% dan sukses di LCD
    ota_snap.ota_progress_pct = 100;
    hud_state_update(&ota_snap);

    ESP_LOGI(TAG, "OTA Flash success! Scheduled restart.");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"OTA Flash Success! Restarting...\"}");

    xTaskCreate(restart_timer_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t config_get_uri = {
            .uri = "/api/config",
            .method = HTTP_GET,
            .handler = config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_get_uri);

        httpd_uri_t config_post_uri = {
            .uri = "/api/config",
            .method = HTTP_POST,
            .handler = config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_post_uri);

        httpd_uri_t ota_uri = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = ota_update_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_uri);

        httpd_uri_t restart_uri = {
            .uri = "/api/restart",
            .method = HTTP_POST,
            .handler = restart_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &restart_uri);

        httpd_uri_t buzzer_test_uri = {
            .uri = "/api/buzzer/test",
            .method = HTTP_POST,
            .handler = buzzer_test_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &buzzer_test_uri);

        httpd_uri_t time_uri = {
            .uri = "/api/time",
            .method = HTTP_POST,
            .handler = time_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &time_uri);

        httpd_uri_t sync_uri = {
            .uri = "/api/sync",
            .method = HTTP_POST,
            .handler = sync_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &sync_uri);

        ESP_LOGI(TAG, "HTTP Server started with REST endpoints & OTA.");
        return server;
    }


    ESP_LOGE(TAG, "Failed to start HTTP server!");
    return NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, ">>> EVENT: WIFI_EVENT_AP_START (SoftAP is officially broadcasting) <<<");
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, ">>> EVENT: WIFI_EVENT_AP_STOP (SoftAP stopped) <<<");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, ">>> EVENT: Station joined SoftAP, AID=%d <<<", event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, ">>> EVENT: Station left SoftAP, AID=%d <<<", event->aid);
    }
}

esp_err_t web_portal_init(void)
{
    ESP_LOGI(TAG, "Starting Wi-Fi SoftAP (%s)...", WIFI_AP_SSID);

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_start();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Wi-Fi SoftAP active. IP: 192.168.4.1 SSID: %s (Ch:%d)",
             WIFI_AP_SSID, WIFI_AP_CHANNEL);

    s_wifi_stopped = false;
    s_last_http_activity_us = esp_timer_get_time();
    s_server = start_webserver();
    if (s_server != NULL) {
        hud_state_t snap;
        hud_state_get_snapshot(&snap);
        snap.wifi_ap_active = true;
        hud_state_update(&snap);
        return ESP_OK;
    }
    return ESP_FAIL;
}

void web_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    esp_wifi_stop();
    s_wifi_stopped = true;

    hud_state_t snap;
    hud_state_get_snapshot(&snap);
    snap.wifi_ap_active = false;
    hud_state_update(&snap);

    ESP_LOGI(TAG, "Wi-Fi SoftAP and Web Server stopped for auto-sleep.");
}
