/**
 * @file dht_sensor.c
 * @brief Ambient temperature and humidity sensor driver (DHT11/DHT22 on GPIO 3).
 */

#include "dht_sensor.h"
#include "hud_state.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT";

static esp_err_t wait_pulse(int level, uint32_t timeout_us, uint32_t *duration_us)
{
    uint32_t start = (uint32_t)esp_timer_get_time();
    while (gpio_get_level(DHT_GPIO_PIN) == level) {
        if ((uint32_t)esp_timer_get_time() - start > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }
    if (duration_us) {
        *duration_us = (uint32_t)esp_timer_get_time() - start;
    }
    return ESP_OK;
}

esp_err_t dht_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing DHT sensor on GPIO %d...", DHT_GPIO_PIN);
    gpio_set_direction(DHT_GPIO_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DHT_GPIO_PIN, GPIO_PULLUP_ENABLE);
    return ESP_OK;
}

esp_err_t dht_sensor_read(dht_type_t type, float *out_temp, float *out_humidity)
{
    if (!out_temp || !out_humidity) return ESP_ERR_INVALID_ARG;

    uint8_t data[5] = {0};

    // 1. Send start signal
    gpio_set_direction(DHT_GPIO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO_PIN, 0);
    // Low for at least 18ms
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT_GPIO_PIN, 1);
    esp_rom_delay_us(30);

    // 2. Switch to input and wait for DHT response
    gpio_set_direction(DHT_GPIO_PIN, GPIO_MODE_INPUT);

    // Disable interrupts during timing-critical bit reception
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    // Wait for DHT response low (~80us) and high (~80us)
    if (wait_pulse(1, 100, NULL) != ESP_OK ||
        wait_pulse(0, 100, NULL) != ESP_OK ||
        wait_pulse(1, 100, NULL) != ESP_OK) {
        taskEXIT_CRITICAL(&mux);
        return ESP_ERR_TIMEOUT;
    }

    // 3. Read 40 bits
    for (int i = 0; i < 40; i++) {
        // Initial 50us low pulse
        if (wait_pulse(0, 70, NULL) != ESP_OK) {
            taskEXIT_CRITICAL(&mux);
            return ESP_ERR_TIMEOUT;
        }

        // Measure high pulse length (26-28us = 0, 70us = 1)
        uint32_t high_dur = 0;
        if (wait_pulse(1, 90, &high_dur) != ESP_OK) {
            taskEXIT_CRITICAL(&mux);
            return ESP_ERR_TIMEOUT;
        }

        data[i / 8] <<= 1;
        if (high_dur > 40) {
            data[i / 8] |= 1;
        }
    }

    taskEXIT_CRITICAL(&mux);

    // 4. Verify checksum
    uint8_t sum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (sum != data[4]) {
        ESP_LOGW(TAG, "DHT checksum failure: calc=%02X, got=%02X", sum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    // 5. Decode values based on sensor type
    if (type == DHT_TYPE_DHT11) {
        *out_humidity = (float)data[0];
        *out_temp = (float)data[2];
    } else { // DHT22 / AM2302
        int16_t raw_hum = ((int16_t)data[0] << 8) | data[1];
        int16_t raw_temp = (((int16_t)data[2] & 0x7F) << 8) | data[3];
        if (data[2] & 0x80) {
            raw_temp = -raw_temp;
        }
        *out_humidity = (float)raw_hum / 10.0f;
        *out_temp = (float)raw_temp / 10.0f;
    }

    return ESP_OK;
}

static uint8_t s_consecutive_fails = 0;

void dht_sensor_poll(void)
{
    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    if (!snap.dht_enabled) {
        s_consecutive_fails = 0;
        hud_state_set_climate(0.0f, 0.0f, false);
        return;
    }

    float temp = 0.0f;
    float hum = 0.0f;
    esp_err_t ret = dht_sensor_read(snap.dht_type, &temp, &hum);
    if (ret == ESP_OK) {
        s_consecutive_fails = 0;
        hud_state_set_climate(temp, hum, true);
    } else {
        s_consecutive_fails++;
        ESP_LOGD(TAG, "DHT read error: %s (fail count: %d)", esp_err_to_name(ret), s_consecutive_fails);
        // Retain previous reading across transient spikes; only invalidate after 3 consecutive failures
        if (s_consecutive_fails >= 3) {
            hud_state_set_climate(0.0f, 0.0f, false);
        }
    }
}
