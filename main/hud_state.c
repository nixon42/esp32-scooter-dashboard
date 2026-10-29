/**
 * @file hud_state.c
 * @brief Thread-safe telemetry state repository implementation for Honda Vario 125 K60.
 */

#include "hud_state.h"
#include "buzzer.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"


static const char *TAG = "HUD_STATE";

static hud_state_t s_hud_state;
static SemaphoreHandle_t s_hud_mutex = NULL;
static const char *NVS_NAMESPACE = "hud_cfg";
static uint32_t s_ble_disconnect_ms = 0;

esp_err_t hud_state_init(void)
{
    ESP_LOGI(TAG, "Initializing HUD state repository...");

    if (s_hud_mutex == NULL) {
        s_hud_mutex = xSemaphoreCreateMutex();
        if (s_hud_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create state mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize defaults per architecture specification
    memset(&s_hud_state, 0, sizeof(hud_state_t));

    s_hud_state.battery_enabled = true;
    s_hud_state.battery_voltage = 0.0f; // 0.0f = no battery connected (displays --.-V)
    s_hud_state.battery_ratio = 5.54f;

    s_hud_state.vss_enabled = true;
    s_hud_state.speed_kmh = 0.0f;
    s_hud_state.speed_source = SPEED_SOURCE_NONE;
    s_hud_state.pulses_per_meter = 28.5f;

    s_hud_state.dht_enabled = true;
    s_hud_state.dht_type = DHT_TYPE_DHT11; // User uses DHT11
    s_hud_state.temperature_c = 0.0f;
    s_hud_state.humidity_pct = 0.0f;
    s_hud_state.dht_valid = false;         // false = waiting for sensor reading (displays --°C --%)

    s_hud_state.ldr_enabled = true;
    s_hud_state.bl_mode = BL_MODE_AUTO_CLOCK;
    s_hud_state.bl_brightness = 200;
    s_hud_state.bl_inverted = true; // Default Active Low for Nokia 5110 Red PCB
    s_hud_state.ldr_raw_adc = 1500;
    s_hud_state.ldr_threshold_adc = 1400;
    s_hud_state.lcd_contrast = 0x3F;
    s_hud_state.clock_style = CLOCK_STYLE_BIG_DIGITAL;
    s_hud_state.ui_cycle_sec = 5; // Default 5 seconds cycle for Standby screens
    s_hud_state.nav_cycle_sec = 10; // Default 10s Turn / 3s Cruising in Nav mode
    s_hud_state.notif_timeout_sec = 7; // Default 7 seconds auto-close for notifications
    s_hud_state.call_timeout_sec = 15; // Default 15 seconds auto-close for incoming calls
    s_hud_state.alert_cue_enabled = true; // Default eye-catching strobe flash & splash cue active
    s_hud_state.buzzer_enabled = true; // Default buzzer enabled
    s_hud_state.buzzer_pin = 20;       // Default GPIO 20 on ESP32-C3 SuperMini
    s_hud_state.buzzer_type = 0;        // Default Active Buzzer (DC High)
    s_hud_state.buzzer_volume = 80;     // Default 80% volume
    s_hud_state.buzzer_profile = 0;    // Default Modern Chirp profile
    s_hud_state.buzzer_boot_en = true; // Default boot jingle enabled
    s_hud_state.buzzer_notif_en = true;// Default chat/message double chirp enabled
    s_hud_state.buzzer_call_en = true; // Default call ringtone enabled
    s_hud_state.buzzer_nav_en = true;  // Default navigation turn chime enabled
    s_hud_state.wifi_ap_timeout_min = 8;// Default auto-sleep Wi-Fi AP after 8 minutes (0 = always ON)
    s_hud_state.wifi_ap_active = true;  // Starts active on boot

    s_hud_state.ble_connected = false;
    s_hud_state.nav_active = false;
    s_hud_state.nav_icon_valid = false;
    s_hud_state.notification_active = false;
    s_hud_state.phone_battery_pct = 85;
    s_hud_state.phone_battery_valid = false;
    s_hud_state.phone_charging = false;
    s_hud_state.media_notif_enabled = true;
    s_hud_state.music_active = false;
    s_hud_state.music_title[0] = '\0';
    s_hud_state.music_artist[0] = '\0';
    s_hud_state.time_synced = false;
    s_hud_state.boot_logo = BOOT_LOGO_VARIO;

    // Default timezone: Western Indonesia Time (WIB, UTC+7)
    setenv("TZ", "WIB-7", 1);
    tzset();

    // Load any persisted NVS overrides
    hud_state_load_from_nvs();

    // Restore preserved clock time from previous session if available
    hud_state_load_clock_time_from_nvs();

    // Restore preserved weather and forecast cache from previous session if available
    hud_state_load_weather_from_nvs();

    ESP_LOGI(TAG, "HUD state initialized successfully.");
    return ESP_OK;

}

void hud_state_get_snapshot(hud_state_t *dest)
{
    if (!dest) return;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Evaluate active navigation grace period expiry on BLE disconnect
        if (s_hud_state.nav_active && !s_hud_state.ble_connected) {
            if (s_ble_disconnect_ms > 0 && (now_ms - s_ble_disconnect_ms >= 8000)) {
                s_hud_state.nav_active = false;
                s_hud_state.nav_icon_valid = false;
                s_ble_disconnect_ms = 0;
                ESP_LOGI(TAG, "BLE disconnect grace period (8s) expired. Navigation inactive.");
            }
        }
        memcpy(dest, &s_hud_state, sizeof(hud_state_t));
        xSemaphoreGive(s_hud_mutex);
    } else {
        if (s_hud_state.nav_active && !s_hud_state.ble_connected) {
            if (s_ble_disconnect_ms > 0 && (now_ms - s_ble_disconnect_ms >= 8000)) {
                s_hud_state.nav_active = false;
                s_hud_state.nav_icon_valid = false;
                s_ble_disconnect_ms = 0;
            }
        }
        memcpy(dest, &s_hud_state, sizeof(hud_state_t));
    }
}

bool hud_state_get_forecast_1h(const hud_state_t *state, chronos_hourly_forecast_t *out_fc)
{
    if (!state || !out_fc) return false;

    int target_hour = -1;

    if (state->time_synced) {
        time_t now;
        time(&now);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        target_hour = (tm_now.tm_hour + 1) % 24;

        if (state->hourly_forecast[target_hour].valid) {
            *out_fc = state->hourly_forecast[target_hour];
            return true;
        }

        // Search forward up to 3 hours
        for (int off = 2; off <= 4; off++) {
            int h = (tm_now.tm_hour + off) % 24;
            if (state->hourly_forecast[h].valid) {
                *out_fc = state->hourly_forecast[h];
                return true;
            }
        }
    }

    // Fallback: search for any valid hourly forecast entry
    for (int h = 0; h < 24; h++) {
        if (state->hourly_forecast[h].valid) {
            *out_fc = state->hourly_forecast[h];
            return true;
        }
    }

    // Fallback to daily weather if valid
    if (state->weather_valid) {
        out_fc->hour = (target_hour >= 0) ? (uint8_t)target_hour : 0;
        out_fc->temp = state->weather_current_temp;
        out_fc->icon = state->weather_current_icon;
        out_fc->humidity = 0;
        out_fc->wind = 0;
        out_fc->uv = 0;
        out_fc->valid = true;
        return true;
    }

    return false;
}

void hud_state_update(const hud_state_t *src)
{
    if (!src) return;

    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&s_hud_state, src, sizeof(hud_state_t));
        if (s_hud_state.nav_active) {
            s_ble_disconnect_ms = 0;
        }
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_vss_speed(float speed_kmh)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.speed_kmh = speed_kmh;
        s_hud_state.speed_source = SPEED_SOURCE_VSS;
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_gps_speed(float speed_kmh, bool valid)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.ble_gps_speed = speed_kmh;
        s_hud_state.ble_gps_valid = valid;
        if (!s_hud_state.vss_enabled && valid) {
            s_hud_state.speed_kmh = speed_kmh;
            s_hud_state.speed_source = SPEED_SOURCE_BLE_GPS;
        }
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_battery(float voltage)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.battery_voltage = voltage;
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_climate(float temp_c, float humidity, bool valid)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.temperature_c = temp_c;
        s_hud_state.humidity_pct = humidity;
        s_hud_state.dht_valid = valid;
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_ldr(uint16_t ldr_adc)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.ldr_raw_adc = ldr_adc;
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_phone_battery(uint8_t pct, bool charging, bool valid)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.phone_battery_pct = (pct > 100) ? 100 : pct;
        s_hud_state.phone_charging = charging;
        s_hud_state.phone_battery_valid = valid;
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_music(bool active, const char *title, const char *artist)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_hud_state.music_active = active;
        if (title) {
            strncpy(s_hud_state.music_title, title, sizeof(s_hud_state.music_title) - 1);
            s_hud_state.music_title[sizeof(s_hud_state.music_title) - 1] = '\0';
        } else {
            s_hud_state.music_title[0] = '\0';
        }
        if (artist) {
            strncpy(s_hud_state.music_artist, artist, sizeof(s_hud_state.music_artist) - 1);
            s_hud_state.music_artist[sizeof(s_hud_state.music_artist) - 1] = '\0';
        } else {
            s_hud_state.music_artist[0] = '\0';
        }
        xSemaphoreGive(s_hud_mutex);
    }
}

void hud_state_set_ble_connected(bool connected)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_hud_state.ble_connected = connected;
        if (connected) {
            s_ble_disconnect_ms = 0;
        }
        xSemaphoreGive(s_hud_mutex);
    } else {
        s_hud_state.ble_connected = connected;
        if (connected) {
            s_ble_disconnect_ms = 0;
        }
    }
}

void hud_state_set_ble_disconnected(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_hud_state.ble_connected = false;
        s_hud_state.notification_active = false;
        // Note: phone_battery_pct and phone_battery_valid are preserved across disconnects
        // so battery telemetry does not vanish intermittently during momentary BLE renegotiation.
        s_hud_state.music_active = false;
        s_hud_state.ble_gps_valid = false;
        if (!s_hud_state.vss_enabled) {
            s_hud_state.speed_kmh = 0.0f;
            s_hud_state.speed_source = SPEED_SOURCE_NONE;
        }
        // Grace period: if navigation is active, preserve nav_active for 8 seconds
        // so momentary GATT 133 or sync reconnects do not drop the turn HUD to standby.
        if (s_hud_state.nav_active) {
            if (s_ble_disconnect_ms == 0) {
                s_ble_disconnect_ms = now_ms;
                ESP_LOGI(TAG, "BLE disconnected during active nav; starting 8s grace period.");
            }
        } else {
            s_hud_state.nav_active = false;
            s_ble_disconnect_ms = 0;
        }
        // Note: s_hud_state.time_synced is intentionally preserved across disconnects!
        xSemaphoreGive(s_hud_mutex);
    } else {
        s_hud_state.ble_connected = false;
        s_hud_state.notification_active = false;
        // Preserved across disconnects
        s_hud_state.music_active = false;
        s_hud_state.ble_gps_valid = false;
        if (!s_hud_state.vss_enabled) {
            s_hud_state.speed_kmh = 0.0f;
            s_hud_state.speed_source = SPEED_SOURCE_NONE;
        }
        if (s_hud_state.nav_active) {
            if (s_ble_disconnect_ms == 0) {
                s_ble_disconnect_ms = now_ms;
            }
        } else {
            s_hud_state.nav_active = false;
            s_ble_disconnect_ms = 0;
        }
    }
}

void hud_state_set_time_synced(bool synced)
{
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_hud_state.time_synced = synced;
        xSemaphoreGive(s_hud_mutex);
    } else {
        s_hud_state.time_synced = synced;
    }
}


void hud_state_save_clock_time(time_t epoch, bool synced)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;

    nvs_set_u64(nvs, "clk_epoch", (uint64_t)epoch);
    nvs_set_u8(nvs, "clk_synced", synced ? 1 : 0);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void hud_state_sync_clock_to_nvs(void)
{
    bool synced = false;
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        synced = s_hud_state.time_synced;
        xSemaphoreGive(s_hud_mutex);
    }
    if (synced) {
        time_t now;
        time(&now);
        if (now > 1700000000) {
            hud_state_save_clock_time(now, true);
        }
    }
}

bool hud_state_load_clock_time_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    uint64_t saved_epoch = 0;
    uint8_t clk_synced = 0;
    bool success = false;

    if (nvs_get_u64(nvs, "clk_epoch", &saved_epoch) == ESP_OK &&
        nvs_get_u8(nvs, "clk_synced", &clk_synced) == ESP_OK) {
        if (clk_synced && saved_epoch > 1700000000ULL) {
            struct timeval tv = { .tv_sec = (time_t)saved_epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);

            if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_hud_state.time_synced = true;
                xSemaphoreGive(s_hud_mutex);
            } else {
                s_hud_state.time_synced = true;
            }

            time_t t_now = (time_t)saved_epoch;
            struct tm tm_saved;
            localtime_r(&t_now, &tm_saved);
            ESP_LOGI(TAG, "Restored preserved clock time from NVS: %02d:%02d:%02d %02d/%02d/%04d (epoch=%llu)",
                     tm_saved.tm_hour, tm_saved.tm_min, tm_saved.tm_sec,
                     tm_saved.tm_mday, tm_saved.tm_mon + 1, tm_saved.tm_year + 1900,
                     (unsigned long long)saved_epoch);
            success = true;
        }
    }

    nvs_close(nvs);
    return success;
}

typedef struct {
    uint64_t saved_epoch;
    int8_t current_temp;
    uint8_t current_icon;
    char city[32];
    char region[32];
    chronos_hourly_forecast_t hourly[24];
} hud_weather_nvs_cache_t;

static hud_weather_nvs_cache_t s_last_saved_weather;
static bool s_has_saved_weather_once = false;

void hud_state_save_weather_to_nvs(void)
{
    hud_weather_nvs_cache_t cache;
    memset(&cache, 0, sizeof(cache));

    time_t now;
    time(&now);
    cache.saved_epoch = (uint64_t)now;

    bool valid = false;
    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        valid = s_hud_state.weather_valid;
        if (valid) {
            cache.current_temp = s_hud_state.weather_current_temp;
            cache.current_icon = s_hud_state.weather_current_icon;
            strncpy(cache.city, s_hud_state.weather_city, sizeof(cache.city) - 1);
            strncpy(cache.region, s_hud_state.weather_region, sizeof(cache.region) - 1);
            memcpy(cache.hourly, s_hud_state.hourly_forecast, sizeof(cache.hourly));
        }
        xSemaphoreGive(s_hud_mutex);
    } else {
        valid = s_hud_state.weather_valid;
        if (valid) {
            cache.current_temp = s_hud_state.weather_current_temp;
            cache.current_icon = s_hud_state.weather_current_icon;
            strncpy(cache.city, s_hud_state.weather_city, sizeof(cache.city) - 1);
            strncpy(cache.region, s_hud_state.weather_region, sizeof(cache.region) - 1);
            memcpy(cache.hourly, s_hud_state.hourly_forecast, sizeof(cache.hourly));
        }
    }

    if (!valid) return;

    // Deduplication check: if weather payload has NOT changed, do NOT write to flash!
    if (s_has_saved_weather_once) {
        if (cache.current_temp == s_last_saved_weather.current_temp &&
            cache.current_icon == s_last_saved_weather.current_icon &&
            strcmp(cache.city, s_last_saved_weather.city) == 0 &&
            strcmp(cache.region, s_last_saved_weather.region) == 0 &&
            memcmp(cache.hourly, s_last_saved_weather.hourly, sizeof(cache.hourly)) == 0) {
            return;
        }
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;

    nvs_set_blob(nvs, "w_cache", &cache, sizeof(cache));
    nvs_commit(nvs);
    nvs_close(nvs);

    s_last_saved_weather = cache;
    s_has_saved_weather_once = true;
    ESP_LOGI(TAG, "Weather cache saved to NVS (city='%s', temp=%d).", cache.city, cache.current_temp);
}

bool hud_state_load_weather_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    hud_weather_nvs_cache_t cache;
    size_t required_size = sizeof(cache);
    err = nvs_get_blob(nvs, "w_cache", &cache, &required_size);
    nvs_close(nvs);

    if (err != ESP_OK || required_size != sizeof(cache)) return false;

    // Check age: keep cache valid for up to 24 hours (86400s)
    time_t now;
    time(&now);
    if (now > 1700000000 && cache.saved_epoch > 1700000000) {
        if (now > (time_t)cache.saved_epoch && (now - (time_t)cache.saved_epoch) > 86400) {
            ESP_LOGW(TAG, "Weather cache in NVS is older than 24h, ignoring.");
            return false;
        }
    }

    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_hud_state.weather_current_temp = cache.current_temp;
        s_hud_state.weather_current_icon = cache.current_icon;
        strncpy(s_hud_state.weather_city, cache.city, sizeof(s_hud_state.weather_city) - 1);
        strncpy(s_hud_state.weather_region, cache.region, sizeof(s_hud_state.weather_region) - 1);
        memcpy(s_hud_state.hourly_forecast, cache.hourly, sizeof(s_hud_state.hourly_forecast));
        s_hud_state.weather_valid = true;
        xSemaphoreGive(s_hud_mutex);
    } else {
        s_hud_state.weather_current_temp = cache.current_temp;
        s_hud_state.weather_current_icon = cache.current_icon;
        strncpy(s_hud_state.weather_city, cache.city, sizeof(s_hud_state.weather_city) - 1);
        strncpy(s_hud_state.weather_region, cache.region, sizeof(s_hud_state.weather_region) - 1);
        memcpy(s_hud_state.hourly_forecast, cache.hourly, sizeof(s_hud_state.hourly_forecast));
        s_hud_state.weather_valid = true;
    }

    s_last_saved_weather = cache;
    s_has_saved_weather_once = true;
    ESP_LOGI(TAG, "Restored cached weather from NVS: City='%s', Temp=%d°C", cache.city, cache.current_temp);
    return true;
}

void hud_state_save_to_nvs(void)

{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", NVS_NAMESPACE, esp_err_to_name(err));
        return;
    }

    if (s_hud_mutex != NULL && xSemaphoreTake(s_hud_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        nvs_set_u8(nvs, "bat_en", s_hud_state.battery_enabled ? 1 : 0);
        uint32_t bat_ratio_bits;
        memcpy(&bat_ratio_bits, &s_hud_state.battery_ratio, sizeof(float));
        nvs_set_u32(nvs, "bat_ratio", bat_ratio_bits);

        nvs_set_u8(nvs, "vss_en", s_hud_state.vss_enabled ? 1 : 0);
        uint32_t ppm_bits;
        memcpy(&ppm_bits, &s_hud_state.pulses_per_meter, sizeof(float));
        nvs_set_u32(nvs, "ppm", ppm_bits);

        nvs_set_u8(nvs, "dht_en", s_hud_state.dht_enabled ? 1 : 0);
        nvs_set_u8(nvs, "dht_type", (uint8_t)s_hud_state.dht_type);

        nvs_set_u8(nvs, "ldr_en", s_hud_state.ldr_enabled ? 1 : 0);
        nvs_set_u8(nvs, "bl_mode", (uint8_t)s_hud_state.bl_mode);
        nvs_set_u8(nvs, "bl_bright", s_hud_state.bl_brightness);
        nvs_set_u8(nvs, "bl_inv", s_hud_state.bl_inverted ? 1 : 0);
        nvs_set_u16(nvs, "ldr_thresh", s_hud_state.ldr_threshold_adc);
        nvs_set_u8(nvs, "contrast", s_hud_state.lcd_contrast);
        nvs_set_u8(nvs, "clk_style", (uint8_t)s_hud_state.clock_style);
        nvs_set_u8(nvs, "boot_logo", (uint8_t)s_hud_state.boot_logo);
        nvs_set_u8(nvs, "media_en", s_hud_state.media_notif_enabled ? 1 : 0);
        nvs_set_u8(nvs, "ui_cycle", s_hud_state.ui_cycle_sec);
        nvs_set_u8(nvs, "nav_cycle", s_hud_state.nav_cycle_sec);
        nvs_set_u8(nvs, "notif_time", s_hud_state.notif_timeout_sec);
        nvs_set_u8(nvs, "call_time", s_hud_state.call_timeout_sec);
        nvs_set_u8(nvs, "alert_cue", s_hud_state.alert_cue_enabled ? 1 : 0);
        nvs_set_u8(nvs, "buzz_en", s_hud_state.buzzer_enabled ? 1 : 0);
        nvs_set_u8(nvs, "buzz_pin", s_hud_state.buzzer_pin);
        nvs_set_u8(nvs, "buzz_type", s_hud_state.buzzer_type);
        nvs_set_u8(nvs, "buzz_vol", s_hud_state.buzzer_volume);
        nvs_set_u8(nvs, "buzz_prof", s_hud_state.buzzer_profile);
        nvs_set_u8(nvs, "buzz_boot", s_hud_state.buzzer_boot_en ? 1 : 0);
        nvs_set_u8(nvs, "buzz_notif", s_hud_state.buzzer_notif_en ? 1 : 0);
        nvs_set_u8(nvs, "buzz_call", s_hud_state.buzzer_call_en ? 1 : 0);
        nvs_set_u8(nvs, "buzz_nav", s_hud_state.buzzer_nav_en ? 1 : 0);
        nvs_set_u8(nvs, "wifi_time", s_hud_state.wifi_ap_timeout_min);

        esp_err_t c_err = nvs_commit(nvs);
        if (c_err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(c_err));
        } else {
            ESP_LOGI(TAG, "HUD configuration committed to NVS.");
        }
        xSemaphoreGive(s_hud_mutex);
    }

    nvs_close(nvs);
}

void hud_state_load_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS configuration found, using defaults.");
        return;
    }

    uint8_t u8_val = 0;
    uint16_t u16_val = 0;
    uint32_t u32_val = 0;

    if (nvs_get_u8(nvs, "bat_en", &u8_val) == ESP_OK) s_hud_state.battery_enabled = (u8_val != 0);
    if (nvs_get_u32(nvs, "bat_ratio", &u32_val) == ESP_OK) {
        float fval;
        memcpy(&fval, &u32_val, sizeof(float));
        s_hud_state.battery_ratio = fval;
    }
    if (nvs_get_u8(nvs, "vss_en", &u8_val) == ESP_OK) s_hud_state.vss_enabled = (u8_val != 0);
    if (nvs_get_u32(nvs, "ppm", &u32_val) == ESP_OK) {
        float fval;
        memcpy(&fval, &u32_val, sizeof(float));
        s_hud_state.pulses_per_meter = fval;
    }
    if (nvs_get_u8(nvs, "dht_en", &u8_val) == ESP_OK) s_hud_state.dht_enabled = (u8_val != 0);
    if (nvs_get_u8(nvs, "dht_type", &u8_val) == ESP_OK) s_hud_state.dht_type = (dht_type_t)u8_val;
    if (nvs_get_u8(nvs, "ldr_en", &u8_val) == ESP_OK) s_hud_state.ldr_enabled = (u8_val != 0);
    if (nvs_get_u8(nvs, "bl_mode", &u8_val) == ESP_OK) s_hud_state.bl_mode = (backlight_mode_t)u8_val;
    if (nvs_get_u8(nvs, "bl_bright", &u8_val) == ESP_OK) s_hud_state.bl_brightness = u8_val;
    if (nvs_get_u8(nvs, "bl_inv", &u8_val) == ESP_OK) {
        s_hud_state.bl_inverted = (u8_val != 0);
    } else {
        s_hud_state.bl_inverted = true; // Default Active Low for Nokia 5110 red PCB
    }
    if (nvs_get_u16(nvs, "ldr_thresh", &u16_val) == ESP_OK) s_hud_state.ldr_threshold_adc = u16_val;
    if (nvs_get_u8(nvs, "contrast", &u8_val) == ESP_OK) {
        if (u8_val >= 0x38 && u8_val <= 0x48) {
            s_hud_state.lcd_contrast = u8_val;
        }
    }
    if (nvs_get_u8(nvs, "clk_style", &u8_val) == ESP_OK) {
        if (u8_val < CLOCK_STYLE_MAX) {
            s_hud_state.clock_style = (clock_style_t)u8_val;
        }
    } else {
        s_hud_state.clock_style = CLOCK_STYLE_BIG_DIGITAL;
    }
    if (nvs_get_u8(nvs, "boot_logo", &u8_val) == ESP_OK) {
        if (u8_val < BOOT_LOGO_MAX) {
            s_hud_state.boot_logo = (boot_logo_t)u8_val;
        }
    }
    if (nvs_get_u8(nvs, "media_en", &u8_val) == ESP_OK) s_hud_state.media_notif_enabled = (u8_val != 0);
    if (nvs_get_u8(nvs, "ui_cycle", &u8_val) == ESP_OK) {
        s_hud_state.ui_cycle_sec = u8_val;
    } else {
        s_hud_state.ui_cycle_sec = 5;
    }
    if (nvs_get_u8(nvs, "nav_cycle", &u8_val) == ESP_OK) {
        s_hud_state.nav_cycle_sec = u8_val;
    } else {
        s_hud_state.nav_cycle_sec = 10;
    }
    if (nvs_get_u8(nvs, "notif_time", &u8_val) == ESP_OK) {
        if (u8_val >= 3 && u8_val <= 30) s_hud_state.notif_timeout_sec = u8_val;
    } else {
        s_hud_state.notif_timeout_sec = 7;
    }
    if (nvs_get_u8(nvs, "call_time", &u8_val) == ESP_OK) {
        if (u8_val <= 60) s_hud_state.call_timeout_sec = u8_val;
    } else {
        s_hud_state.call_timeout_sec = 15;
    }
    if (nvs_get_u8(nvs, "alert_cue", &u8_val) == ESP_OK) {
        s_hud_state.alert_cue_enabled = (u8_val != 0);
    } else {
        s_hud_state.alert_cue_enabled = true;
    }
    if (nvs_get_u8(nvs, "buzz_en", &u8_val) == ESP_OK) {
        s_hud_state.buzzer_enabled = (u8_val != 0);
    } else {
        s_hud_state.buzzer_enabled = true;
    }
    if (nvs_get_u8(nvs, "buzz_pin", &u8_val) == ESP_OK) {
        if (u8_val == 20 || u8_val == 21 || u8_val == 9) s_hud_state.buzzer_pin = u8_val;
    } else {
        s_hud_state.buzzer_pin = 20;
    }
    if (nvs_get_u8(nvs, "buzz_type", &u8_val) == ESP_OK) {
        if (u8_val <= 1) s_hud_state.buzzer_type = u8_val;
    } else {
        s_hud_state.buzzer_type = 0;
    }
    if (nvs_get_u8(nvs, "buzz_vol", &u8_val) == ESP_OK) {
        if (u8_val >= 10 && u8_val <= 100) s_hud_state.buzzer_volume = u8_val;
    } else {
        s_hud_state.buzzer_volume = 80;
    }
    if (nvs_get_u8(nvs, "buzz_prof", &u8_val) == ESP_OK) {
        if (u8_val < BUZZER_PROFILE_MAX) s_hud_state.buzzer_profile = u8_val;
    } else {
        s_hud_state.buzzer_profile = 0;
    }
    if (nvs_get_u8(nvs, "buzz_boot", &u8_val) == ESP_OK) {
        s_hud_state.buzzer_boot_en = (u8_val != 0);
    } else {
        s_hud_state.buzzer_boot_en = true;
    }
    if (nvs_get_u8(nvs, "buzz_notif", &u8_val) == ESP_OK) {
        s_hud_state.buzzer_notif_en = (u8_val != 0);
    } else {
        s_hud_state.buzzer_notif_en = true;
    }
    if (nvs_get_u8(nvs, "buzz_call", &u8_val) == ESP_OK) {
        s_hud_state.buzzer_call_en = (u8_val != 0);
    } else {
        s_hud_state.buzzer_call_en = true;
    }
    if (nvs_get_u8(nvs, "buzz_nav", &u8_val) == ESP_OK) {
        s_hud_state.buzzer_nav_en = (u8_val != 0);
    } else {
        s_hud_state.buzzer_nav_en = true;
    }
    if (nvs_get_u8(nvs, "wifi_time", &u8_val) == ESP_OK) {
        if (u8_val <= 60) s_hud_state.wifi_ap_timeout_min = u8_val;
    } else {
        s_hud_state.wifi_ap_timeout_min = 8; // Default 8 menit (bisa diatur lewat web)
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "HUD configuration loaded from NVS.");
}
