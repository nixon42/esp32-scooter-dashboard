/**
 * @file voltmeter.c
 * @brief Aki Voltmeter implementation using ESP32-C3 ADC oneshot & calibration.
 */

#include "voltmeter.h"
#include "hud_state.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "VOLTMETER";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_has_cali = false;
static bool s_enabled = true;
static float s_ratio = DEFAULT_VOLT_RATIO;
static float s_smooth_voltage = 0.0f;

adc_oneshot_unit_handle_t voltmeter_get_adc_handle(void)
{
    if (s_adc_handle == NULL) {
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_adc_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create ADC1 unit handle: %s", esp_err_to_name(ret));
            return NULL;
        }
    }
    return s_adc_handle;
}

esp_err_t voltmeter_init(void)
{
    ESP_LOGI(TAG, "Initializing Aki Voltmeter on GPIO 0 (ADC1_CH0)...");

    hud_state_t snap;
    hud_state_get_snapshot(&snap);
    s_enabled = snap.battery_enabled;
    s_ratio = snap.battery_ratio;

    s_adc_handle = voltmeter_get_adc_handle();
    if (!s_adc_handle) {
        return ESP_FAIL;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    esp_err_t ret = adc_oneshot_config_channel(s_adc_handle, VOLTMETER_ADC_CHANNEL, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC1 channel 0: %s", esp_err_to_name(ret));
        return ret;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = VOLTMETER_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle);
    if (ret == ESP_OK) {
        s_has_cali = true;
        ESP_LOGI(TAG, "ADC curve fitting calibration initialized.");
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
    if (ret == ESP_OK) {
        s_has_cali = true;
        ESP_LOGI(TAG, "ADC line fitting calibration initialized.");
    }
#endif

    return ESP_OK;
}

esp_err_t voltmeter_read(float *out_voltage)
{
    if (!out_voltage) return ESP_ERR_INVALID_ARG;
    if (!s_enabled || !s_adc_handle) {
        *out_voltage = 0.0f;
        return ESP_ERR_INVALID_STATE;
    }

    int raw_sum = 0;
    const int SAMPLES = 8;
    for (int i = 0; i < SAMPLES; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, VOLTMETER_ADC_CHANNEL, &raw);
        if (ret == ESP_OK) {
            raw_sum += raw;
        }
    }
    int raw_avg = raw_sum / SAMPLES;

    int voltage_mv = 0;
    if (s_has_cali) {
        adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &voltage_mv);
    } else {
        voltage_mv = (raw_avg * 3100) / 4095;
    }

    float battery_voltage = ((float)voltage_mv * s_ratio) / 1000.0f;
    // Suppress floating ADC noise: a real 12V scooter battery is >= 9.0V.
    // If under 5.0V, the pin is floating / disconnected.
    if (battery_voltage < 5.0f) {
        battery_voltage = 0.0f;
        s_smooth_voltage = 0.0f;
    } else {
        if (s_smooth_voltage < 5.0f) {
            s_smooth_voltage = battery_voltage; // Immediate lock on initial read
        } else {
            // Low-pass exponential filter: reject stator/regulator ripple
            s_smooth_voltage = (0.75f * s_smooth_voltage) + (0.25f * battery_voltage);
        }
    }
    *out_voltage = s_smooth_voltage;

    hud_state_set_battery(s_smooth_voltage);
    return ESP_OK;
}

void voltmeter_set_enabled(bool enabled)
{
    s_enabled = enabled;
    hud_state_t snap;
    hud_state_get_snapshot(&snap);
    snap.battery_enabled = enabled;
    if (!enabled) {
        snap.battery_voltage = 0.0f;
    }
    hud_state_update(&snap);
}

void voltmeter_set_ratio(float ratio)
{
    if (ratio > 0.1f && ratio < 20.0f) {
        s_ratio = ratio;
    }
}
