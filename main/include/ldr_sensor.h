/**
 * @file ldr_sensor.h
 * @brief LDR Ambient Light sensor and automatic backlight controller.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LDR_ADC_CHANNEL         ADC_CHANNEL_1 // GPIO 1 (ADC1_CH1)
#define DEFAULT_LDR_THRESHOLD   1400          // ADC threshold for dark/bright

esp_err_t ldr_sensor_init(void);
esp_err_t ldr_sensor_read(uint16_t *out_raw_adc);
void ldr_sensor_poll_and_update_backlight(void);

#ifdef __cplusplus
}
#endif
