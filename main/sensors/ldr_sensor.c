/**
 * @file ldr_sensor.c
 * @brief LDR Ambient Light sensor and automatic backlight controller implementation.
 */

#include "ldr_sensor.h"
#include "voltmeter.h"
#include "hud_state.h"
#include "hud_ui.h"
#include "pcd8544.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include <time.h>

static const char *TAG = "LDR_SENSOR";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static bool s_configured = false;

esp_err_t ldr_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing LDR light sensor on GPIO 1 (ADC1_CH1)...");

    s_adc_handle = voltmeter_get_adc_handle();
    if (!s_adc_handle) {
        ESP_LOGE(TAG, "Unable to get ADC1 unit for LDR");
        return ESP_FAIL;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    esp_err_t ret = adc_oneshot_config_channel(s_adc_handle, LDR_ADC_CHANNEL, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC1 channel 1: %s", esp_err_to_name(ret));
        return ret;
    }

    s_configured = true;
    ESP_LOGI(TAG, "LDR light sensor initialized.");
    return ESP_OK;
}

esp_err_t ldr_sensor_read(uint16_t *out_raw_adc)
{
    if (!out_raw_adc) return ESP_ERR_INVALID_ARG;
    if (!s_configured || !s_adc_handle) return ESP_ERR_INVALID_STATE;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, LDR_ADC_CHANNEL, &raw);
    if (ret == ESP_OK) {
        *out_raw_adc = (uint16_t)raw;
        hud_state_set_ldr((uint16_t)raw);
    }
    return ret;
}

void ldr_sensor_poll_and_update_backlight(void)
{
    if (hud_ui_is_alert_cue_active()) {
        return; // Preserve eye-catch backlight strobe without interference
    }

    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    uint16_t raw_adc = 0;
    if (snap.ldr_enabled) {
        ldr_sensor_read(&raw_adc);
    }

    switch (snap.bl_mode) {
        case BL_MODE_MANUAL_OFF:
            pcd8544_set_backlight_raw(0);
            break;

        case BL_MODE_MANUAL_ON:
            pcd8544_set_backlight_raw(snap.bl_brightness);
            break;

        case BL_MODE_AUTO_LDR:
            if (snap.ldr_enabled) {
                uint16_t thresh = snap.ldr_threshold_adc > 0 ? snap.ldr_threshold_adc : 1400;
                const uint16_t hyst = 200;
                uint16_t lower_bound = (thresh > hyst) ? (thresh - hyst) : 0;
                uint16_t upper_bound = thresh + hyst;

                if (raw_adc < lower_bound) {
                    // Dark environment / tunnel entry: turn on backlight
                    pcd8544_set_backlight_raw(snap.bl_brightness > 0 ? snap.bl_brightness : 200);
                } else if (raw_adc > upper_bound) {
                    // Bright daylight / tunnel exit: turn off backlight
                    pcd8544_set_backlight_raw(0);
                }
                // In deadband [lower_bound, upper_bound]: retain current state
            } else {
                pcd8544_set_backlight_raw(snap.bl_brightness);
            }
            break;

        case BL_MODE_AUTO_CLOCK: {
            bool is_night = false;
            if (snap.time_synced) {
                time_t now;
                time(&now);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                int total_mins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
                // Night / Evening schedule: 16:30 (4.30 sore) to 06:30 (6.30 pagi) -> Backlight ON
                // Daytime schedule:        06:30 (6.30 pagi) to 16:30 (4.30 sore) -> Backlight OFF
                is_night = (total_mins >= (16 * 60 + 30) || total_mins < (6 * 60 + 30));
            } else if (snap.ldr_enabled) {
                // Fallback to LDR sensor if phone time is not yet synced
                uint16_t thresh = snap.ldr_threshold_adc > 0 ? snap.ldr_threshold_adc : 1400;
                is_night = (raw_adc < thresh);
            } else {
                // Safe default if neither time nor LDR is available
                is_night = true;
            }

            pcd8544_set_backlight_raw(is_night ? (snap.bl_brightness > 0 ? snap.bl_brightness : 200) : 0);
            break;
        }
    }
}
