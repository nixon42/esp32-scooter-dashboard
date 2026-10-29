/**
 * @file voltmeter.h
 * @brief Aki (Battery) Voltmeter driver using ESP32-C3 ADC oneshot & calibration.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOLTMETER_ADC_CHANNEL   ADC_CHANNEL_0 // GPIO 0 (ADC1_CH0)
#define DEFAULT_VOLT_RATIO      5.54f        // (10k + 2.2k) / 2.2k

esp_err_t voltmeter_init(void);
esp_err_t voltmeter_read(float *out_voltage);
void voltmeter_set_enabled(bool enabled);
void voltmeter_set_ratio(float ratio);
adc_oneshot_unit_handle_t voltmeter_get_adc_handle(void);

#ifdef __cplusplus
}
#endif
