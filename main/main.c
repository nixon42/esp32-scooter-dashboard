/**
 * @file main.c
 * @brief DIY Smart Motorcycle HUD & Telemetry Dashboard - Honda Vario 125 K60
 * Target: ESP32-C3 SuperMini + Nokia 5110 LCD (PCD8544)
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "hud_state.h"
#include "pcd8544.h"
#include "hud_ui.h"
#include "chronos_ble.h"
#include "voltmeter.h"
#include "speedometer.h"
#include "dht_sensor.h"
#include "ldr_sensor.h"
#include "buzzer.h"
#include "web_portal.h"

static const char *TAG = "VARIO_HUD";

/**
 * @brief FreeRTOS task refreshing Nokia 5110 LCD at 10 Hz (every 100 ms).
 */
static void display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Display render task started.");
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(100);

    static hud_state_t snapshot;

    while (1) {
        hud_state_get_snapshot(&snapshot);
        hud_ui_render_frame(&snapshot);
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

/**
 * @brief FreeRTOS task polling sensors and calculating speedometer frequency.
 */
static void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor acquisition task started.");
    const TickType_t step_delay = pdMS_TO_TICKS(100);
    uint32_t sec_counter = 0;
    uint32_t total_sec = 0;
    uint32_t nvs_save_timer_sec = 0;
    uint32_t ble_sync_timer_sec = 0;
    bool last_ble_conn = false;

    while (1) {
        vTaskDelay(step_delay);

        // Update hardware VSS frequency every 100ms
        speedometer_poll_update(100);

        // Update ambient light sensor & backlight every 200ms (2 x 100ms) for rapid tunnel response
        if (sec_counter % 2 == 0) {
            ldr_sensor_poll_and_update_backlight();
        }

        // Every 1 second (10 x 100ms)
        sec_counter++;
        if (sec_counter >= 10) {
            sec_counter = 0;
            total_sec++;
            nvs_save_timer_sec++;
            ble_sync_timer_sec++;

            float voltage = 0.0f;
            voltmeter_read(&voltage);

            // Poll ambient DHT11/22 sensor every 2 seconds (strictly adhering to sensor stabilization spec)
            if (total_sec % 2 == 0) {
                dht_sensor_poll();
            }

            // Check Wi-Fi SoftAP auto-sleep timeout (stops SoftAP after inactive period to keep ESP32 cool & BLE priority)
            web_portal_check_auto_sleep();

            // Periodic checkpoint of running clock time to NVS every 300 seconds (5 minutes) if synced
            // This protects flash memory endurance while ensuring time is preserved across reboots.
            if (nvs_save_timer_sec >= 300) {
                nvs_save_timer_sec = 0;
                hud_state_sync_clock_to_nvs();
            }

            // Check BLE connection state transitions
            bool ble_conn = chronos_ble_is_connected();
            hud_state_t sync_snap;
            hud_state_get_snapshot(&sync_snap);

            // Active sync of weather, time, and phone battery:
            // Fast poll every 15s if weather, time, or phone battery is not yet received;
            // then every 900s (15 minutes) once valid data is present.
            bool fully_synced = (sync_snap.weather_valid && sync_snap.time_synced && sync_snap.phone_battery_valid);
            uint32_t sync_interval = fully_synced ? 900 : 15;

            // When newly connected, trigger initial sync after a short 1-second stabilization
            if (ble_conn && !last_ble_conn) {
                ble_sync_timer_sec = sync_interval;
            }
            last_ble_conn = ble_conn;

            if (ble_sync_timer_sec >= sync_interval) {
                ble_sync_timer_sec = 0;
                if (ble_conn && (!sync_snap.nav_active || !sync_snap.phone_battery_valid)) {
                    ESP_LOGI(TAG, "Requesting active sync from Chronos app (weather=%d, time=%d, phone_bat=%d)...",
                             sync_snap.weather_valid, sync_snap.time_synced, sync_snap.phone_battery_valid);
                    chronos_ble_request_sync();
                } else if (sync_snap.nav_active) {
                    ESP_LOGD(TAG, "Active navigation running, deferring background sync request.");
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  VARIO 125 OLD LED (K60) SMART HUD & TELEMETRY DASHBOARD");
    ESP_LOGI(TAG, "  ESP32-C3 SuperMini + PCD8544 + Chronos BLE + Web OTA");
    ESP_LOGI(TAG, "==========================================================");

    // 1. Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Shared Telemetry State Repository (loads boot_logo from NVS)
    ESP_ERROR_CHECK(hud_state_init());

    hud_state_t init_snap;
    hud_state_get_snapshot(&init_snap);

    // 3. Initialize Nokia 5110 LCD Hardware SPI & UI Engine early for real-time boot logs
    ESP_LOGI(TAG, "Initializing PCD8544 SPI display...");
    ret = pcd8544_init();
    if (ret == ESP_OK) {
        pcd8544_set_contrast(init_snap.lcd_contrast);
        pcd8544_set_backlight_inverted(init_snap.bl_inverted);
        hud_ui_init();
        hud_ui_boot_init_screen(init_snap.boot_logo);
        hud_ui_boot_log("[ OK ] Mounted NVS");
    } else {
        ESP_LOGE(TAG, "Failed to initialize PCD8544: %s", esp_err_to_name(ret));
    }

    // 4. Initialize TCP/IP Netif & System Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    hud_ui_boot_log("[ OK ] Netif & event");

    // 5. Initialize Modular Hardware Sensors
    voltmeter_init();
    speedometer_init();
    dht_sensor_init();
    ldr_sensor_init();
    ldr_sensor_poll_and_update_backlight();

    // 5b. Initialize Acoustic Buzzer Driver (Optional)
    buzzer_init();
    buzzer_configure(init_snap.buzzer_pin, (buzzer_type_t)init_snap.buzzer_type, init_snap.buzzer_enabled,
                     init_snap.buzzer_volume, (buzzer_profile_t)init_snap.buzzer_profile);
    buzzer_set_event_enables(init_snap.buzzer_boot_en, init_snap.buzzer_notif_en,
                             init_snap.buzzer_call_en, init_snap.buzzer_nav_en);
    if (init_snap.buzzer_enabled) {
        buzzer_play(BUZZER_TONE_BOOT);
    }

    hud_ui_boot_log("[ OK ] Hardware sen");

    // 6. Initialize NimBLE Bluetooth GATT Server for Chronos Companion App
    ESP_LOGI(TAG, "Initializing NimBLE Chronos BLE stack...");
    ret = chronos_ble_init();
    if (ret == ESP_OK) {
        hud_ui_boot_log("[ OK ] NimBLE Chronos");
    } else {
        ESP_LOGE(TAG, "chronos_ble_init failed: %s", esp_err_to_name(ret));
        hud_ui_boot_log("[WARN] BLE failed");
    }

    // 7. Initialize Wi-Fi SoftAP and Embedded Web Portal with Web OTA
    web_portal_init();
    hud_ui_boot_log("[ OK ] Web Portal AP");

    // 8. Launch Dedicated FreeRTOS Tasks
    hud_ui_boot_log("[ OK ] System Ready");
    xTaskCreatePinnedToCore(display_task, "display_task", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "System initialization complete. Dashboard running.");
}
