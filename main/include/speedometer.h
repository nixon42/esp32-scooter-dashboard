/**
 * @file speedometer.h
 * @brief Speedometer driver for Honda Vario 125 VSS pulse sensor (GPIO 5 ISR).
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VSS_GPIO_PIN            5      // GPIO 5 (Vario pink/green pulse wire)
#define DEFAULT_PULSES_PER_MTR  28.5f  // Pulses per meter calibration

esp_err_t speedometer_init(void);
void speedometer_set_enabled(bool enabled);
void speedometer_set_pulses_per_meter(float ppm);
void speedometer_poll_update(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif
