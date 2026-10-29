/**
 * @file hud_state.h
 * @brief Thread-safe telemetry state repository for Honda Vario 125 K60 dashboard.
 * Target: ESP32-C3 SuperMini (FreeRTOS)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/time.h>
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPEED_SOURCE_NONE = 0,
    SPEED_SOURCE_VSS,
    SPEED_SOURCE_BLE_GPS
} speed_source_t;

typedef enum {
    BL_MODE_MANUAL_OFF = 0,
    BL_MODE_MANUAL_ON,
    BL_MODE_AUTO_LDR,
    BL_MODE_AUTO_CLOCK
} backlight_mode_t;

typedef enum {
    DHT_TYPE_DHT11 = 0,
    DHT_TYPE_DHT22
} dht_type_t;

typedef enum {
    BOOT_LOGO_VARIO = 0,
    BOOT_LOGO_ANDROID,
    BOOT_LOGO_ESPRESSIF,
    BOOT_LOGO_LINUX,
    BOOT_LOGO_ARCH,
    BOOT_LOGO_MAX
} boot_logo_t;

typedef enum {
    CLOCK_STYLE_NOKIA_3310 = 0,
    CLOCK_STYLE_BIG_DIGITAL,      // Big 7-Segment Digital (Default / Current layout)
    CLOCK_STYLE_NOKIA_5110_BAR,   // Nokia 5110 Screensaver (Inverted Bar)
    CLOCK_STYLE_ANALOG,           // Classic Analogue Round Dial
    CLOCK_STYLE_NOKIA_8210,       // Nokia 8210 Executive Compact
    CLOCK_STYLE_MAX
} clock_style_t;

// Chronos BLE Notification App Identifiers
#define CHRONOS_APP_RINGER        0x01
#define CHRONOS_APP_DISMISSED     0x02
#define CHRONOS_APP_MESSAGE       0x03
#define CHRONOS_APP_EMAIL         0x04
#define CHRONOS_APP_TWITTER_OLD   0x08
#define CHRONOS_APP_WECHAT        0x09
#define CHRONOS_APP_WHATSAPP      0x0A
#define CHRONOS_APP_GMAIL         0x0B
#define CHRONOS_APP_LINE          0x0E
#define CHRONOS_APP_TWITTER       0x0F
#define CHRONOS_APP_FACEBOOK      0x10
#define CHRONOS_APP_MESSENGER     0x11
#define CHRONOS_APP_INSTAGRAM     0x12
#define CHRONOS_APP_TELEGRAM      0x18
#define CHRONOS_APP_WA_BIZ        0x20
#define CHRONOS_APP_DISCORD       0x30
#define CHRONOS_APP_MAPS          0x35
#define CHRONOS_APP_WAZE          0x36
#define CHRONOS_APP_SYSTEM        0xC0

typedef struct {
    uint8_t hour;       // Hour of forecast (0..23)
    int8_t temp;        // Temperature in Celsius
    uint8_t icon;       // Weather icon code (0..7)
    uint8_t humidity;   // Relative humidity %
    uint16_t wind;      // Wind speed in km/h
    uint8_t uv;         // UV index
    bool valid;         // True if this hour has valid forecast data
} chronos_hourly_forecast_t;

typedef struct {
    // Battery Voltmeter
    bool battery_enabled;
    float battery_voltage;
    float battery_ratio;

    // Speedometer
    bool vss_enabled;
    float speed_kmh;
    speed_source_t speed_source;
    float pulses_per_meter;

    // Ambient Climate
    bool dht_enabled;
    dht_type_t dht_type;
    float temperature_c;
    float humidity_pct;
    bool dht_valid;

    // Light & Backlight
    bool ldr_enabled;
    backlight_mode_t bl_mode;
    uint8_t bl_brightness;       // 0 - 255 (or 0 - 100%)
    bool bl_inverted;            // True for active low (Nokia 5110 Red PCB)
    uint16_t ldr_raw_adc;
    uint16_t ldr_threshold_adc;
    uint8_t lcd_contrast;        // 0x38 - 0x48
    clock_style_t clock_style;   // Standby Clock Layout Style (0=3310, 1=Big Digital, 2=5110 Bar, 3=Analog, 4=8210)
    uint8_t ui_cycle_sec;        // Cycle duration (seconds) for Standby screens (0=disabled)
    uint8_t nav_cycle_sec;       // Navigation cycle duration (0=lock turn screen, e.g. 10=10s turn / 3s cruising)
    uint8_t notif_timeout_sec;   // Auto close timeout for notifications (seconds, e.g. 3..30, default 7)
    uint8_t call_timeout_sec;    // Auto close timeout for calls (seconds, 0=until call ends, or 5..60, default 15)
    bool alert_cue_enabled;      // Eye-catchy flash strobe & pop-up cue before interrupt screens (default true)
    bool buzzer_enabled;         // Optional acoustic buzzer audio alert (default true)
    uint8_t buzzer_pin;          // Buzzer GPIO pin (default 20)
    uint8_t buzzer_type;         // Buzzer type: 0=Active Buzzer (DC High), 1=Passive Buzzer (PWM Tone)
    uint8_t buzzer_volume;       // Buzzer volume percentage: 10..100 % (default 80%)
    uint8_t buzzer_profile;      // Buzzer sound profile/theme: 0=Modern, 1=Soft, 2=Sci-Fi, 3=Minimal, 4=Nokia
    bool buzzer_boot_en;         // Boot up jingle when ignition is turned ON (default true)
    bool buzzer_notif_en;        // Message / chat notification sound (default true)
    bool buzzer_call_en;         // Incoming call ringing melody (default true)
    bool buzzer_nav_en;          // Navigation turn cue alert chime (default true)
    uint8_t wifi_ap_timeout_min; // Wi-Fi SoftAP auto-sleep timeout in minutes (0=always on, 1, 3, 5, 8, 10..30)
    bool wifi_ap_active;         // Status flag: true if Wi-Fi AP is currently running

    // BLE & Chronos Navigation
    bool ble_connected;
    char nav_title[64];
    char nav_duration[32];
    char nav_distance[32];
    char nav_eta[32];
    char nav_directions[64];
    float ble_gps_speed;
    bool ble_gps_valid;
    bool nav_active;
    uint32_t nav_timestamp;
    uint8_t nav_icon_bmp[288];    // 48x48 1-bit bitmap
    bool nav_icon_valid;

    // Notifications
    bool notification_active;
    uint8_t notif_app_id;
    char notif_title[48];
    char notif_message[256];
    uint32_t notif_timestamp;

    // Smartphone Telemetry & Music
    uint8_t phone_battery_pct;
    bool phone_battery_valid;
    bool phone_charging;
    bool media_notif_enabled;
    bool music_active;
    char music_title[64];
    char music_artist[48];

    // System Time
    bool time_synced;

    // Boot Configuration
    boot_logo_t boot_logo;

    // Chronos Weather & Location Telemetry
    bool weather_valid;
    char weather_city[32];
    char weather_region[32];
    int8_t weather_current_temp;
    uint8_t weather_current_icon;
    chronos_hourly_forecast_t hourly_forecast[24];

    // Web OTA Flashing Status
    bool ota_in_progress;
    uint8_t ota_progress_pct;
} hud_state_t;

/**
 * @brief Initialize HUD state repository and synchronization mutex.
 * @return ESP_OK on success, ESP_ERR_* on error.
 */
esp_err_t hud_state_init(void);

/**
 * @brief Retrieve an atomic thread-safe snapshot of active telemetry state.
 * @param dest Pointer to destination hud_state_t struct.
 */
void hud_state_get_snapshot(hud_state_t *dest);

/**
 * @brief Retrieve weather forecast for 1 hour ahead (or next closest valid hour).
 * @param state Pointer to active snapshot.
 * @param out_fc Destination pointer for 1-hour forecast item.
 * @return true if forecast data is available.
 */
bool hud_state_get_forecast_1h(const hud_state_t *state, chronos_hourly_forecast_t *out_fc);

/**
 * @brief Update entire telemetry state atomically.
 * @param src Pointer to new telemetry state.
 */
void hud_state_update(const hud_state_t *src);

/**
 * @brief Targeted setter for vehicle speed from VSS pulse sensor.
 * @param speed_kmh Calculated speed in km/h.
 */
void hud_state_set_vss_speed(float speed_kmh);

/**
 * @brief Targeted setter for vehicle speed from BLE GPS telemetry.
 * @param speed_kmh GPS speed in km/h.
 * @param valid True if GPS fix is active and speed is valid.
 */
void hud_state_set_gps_speed(float speed_kmh, bool valid);

/**
 * @brief Targeted setter for battery voltage.
 * @param voltage Measured and calibrated battery voltage.
 */
void hud_state_set_battery(float voltage);

/**
 * @brief Targeted setter for ambient temperature and humidity.
 * @param temp_c Temperature in degrees Celsius.
 * @param humidity Relative humidity in percent.
 * @param valid True if DHT checksum passed.
 */
void hud_state_set_climate(float temp_c, float humidity, bool valid);

/**
 * @brief Targeted setter for ambient light sensor raw ADC reading.
 * @param ldr_adc Raw ADC reading (0..4095).
 */
void hud_state_set_ldr(uint16_t ldr_adc);

/**
 * @brief Targeted setter for smartphone battery telemetry.
 * @param pct Battery percentage (0..100).
 * @param charging True if smartphone is actively charging.
 * @param valid True if reading is valid.
 */
void hud_state_set_phone_battery(uint8_t pct, bool charging, bool valid);

/**
 * @brief Targeted setter for music playback state and track info.
 * @param active True if music is playing.
 * @param title Song title string.
 * @param artist Artist name string.
 */
void hud_state_set_music(bool active, const char *title, const char *artist);

/**
 * @brief Targeted setter for BLE connection state.
 * @param connected True if BLE is connected.
 */
void hud_state_set_ble_connected(bool connected);

/**
 * @brief Targeted handler for BLE disconnect: resets ephemeral BLE state without affecting clock.
 */
void hud_state_set_ble_disconnected(void);

/**
 * @brief Targeted setter for time sync flag.
 * @param synced True if system time is synced.
 */
void hud_state_set_time_synced(bool synced);

/**
 * @brief Persist system clock timestamp and sync status to NVS.
 * @param epoch Unix timestamp in seconds.
 * @param synced True if time has been synced.
 */
void hud_state_save_clock_time(time_t epoch, bool synced);

/**
 * @brief Reads current time and commits it to NVS if synced.
 */
void hud_state_sync_clock_to_nvs(void);

/**
 * @brief Loads last preserved clock timestamp from NVS and applies to system clock.
 * @return True if a valid timestamp was restored.
 */
bool hud_state_load_clock_time_from_nvs(void);

/**
 * @brief Save configurable settings to Non-Volatile Storage (NVS).
 */
void hud_state_save_to_nvs(void);

/**
 * @brief Load configurable settings from Non-Volatile Storage (NVS).
 */
void hud_state_load_from_nvs(void);

/**
 * @brief Saves current weather and 24h hourly forecast cache to NVS.
 */
void hud_state_save_weather_to_nvs(void);

/**
 * @brief Loads preserved weather and 24h forecast cache from NVS if valid (<24h).
 * @return True if valid weather cache was restored.
 */
bool hud_state_load_weather_from_nvs(void);

#ifdef __cplusplus
}
#endif

