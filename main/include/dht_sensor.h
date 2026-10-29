/**
 * @file dht_sensor.h
 * @brief Ambient temperature and humidity sensor driver (DHT11/DHT22 on GPIO 3).
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hud_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DHT_GPIO_PIN    3 // GPIO 3 (1-wire with 4.7k-10k pull-up to 3.3V)

esp_err_t dht_sensor_init(void);
esp_err_t dht_sensor_read(dht_type_t type, float *out_temp, float *out_humidity);
void dht_sensor_poll(void);

#ifdef __cplusplus
}
#endif
