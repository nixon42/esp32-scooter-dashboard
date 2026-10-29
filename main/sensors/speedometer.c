/**
 * @file speedometer.c
 * @brief Speedometer driver for Honda Vario 125 VSS pulse sensor (GPIO 5 ISR).
 */

#include "speedometer.h"
#include "hud_state.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "SPEEDOMETER";

#define VSS_DEBOUNCE_US 250 // 250 microseconds debounce to reject ignition spark EMI

static volatile uint32_t s_pulse_counter = 0;
static volatile int64_t  s_last_pulse_time_us = 0;
static portMUX_TYPE s_pulse_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool s_vss_enabled = true;
static float s_pulses_per_meter = DEFAULT_PULSES_PER_MTR;
static bool s_isr_installed = false;
static uint32_t s_zero_pulse_ms = 0;

static void IRAM_ATTR vss_gpio_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_pulse_spinlock);
    if ((now - s_last_pulse_time_us) >= VSS_DEBOUNCE_US) {
        s_pulse_counter++;
        s_last_pulse_time_us = now;
    }
    portEXIT_CRITICAL_ISR(&s_pulse_spinlock);
}

esp_err_t speedometer_init(void)
{
    ESP_LOGI(TAG, "Initializing VSS Speedometer on GPIO %d...", VSS_GPIO_PIN);

    s_zero_pulse_ms = 0;
    hud_state_t snap;
    hud_state_get_snapshot(&snap);
    s_vss_enabled = snap.vss_enabled;
    s_pulses_per_meter = snap.pulses_per_meter;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VSS_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Zener / conditioning pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", VSS_GPIO_PIN, esp_err_to_name(ret));
        return ret;
    }

    if (!s_isr_installed) {
        ret = gpio_install_isr_service(0);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            // ESP_ERR_INVALID_STATE means service already installed, which is fine
            s_isr_installed = true;
        } else {
            ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    ret = gpio_isr_handler_add(VSS_GPIO_PIN, vss_gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "VSS Speedometer ISR initialized successfully.");
    return ESP_OK;
}

void speedometer_set_enabled(bool enabled)
{
    s_vss_enabled = enabled;
    s_zero_pulse_ms = 0;
    hud_state_t snap;
    hud_state_get_snapshot(&snap);
    snap.vss_enabled = enabled;
    if (!enabled && snap.ble_gps_valid) {
        snap.speed_kmh = snap.ble_gps_speed;
        snap.speed_source = SPEED_SOURCE_BLE_GPS;
    }
    hud_state_update(&snap);
}

void speedometer_set_pulses_per_meter(float ppm)
{
    if (ppm > 0.1f && ppm < 200.0f) {
        s_pulses_per_meter = ppm;
    }
}

void speedometer_poll_update(uint32_t elapsed_ms)
{
    if (elapsed_ms == 0) return;

    uint32_t pulses = 0;
    portENTER_CRITICAL(&s_pulse_spinlock);
    pulses = s_pulse_counter;
    s_pulse_counter = 0;
    portEXIT_CRITICAL(&s_pulse_spinlock);

    if (!s_vss_enabled) {
        // Hardware VSS disabled: speed comes from Chronos BLE GPS
        s_zero_pulse_ms = 0;
        return;
    }

    if (pulses > 0) {
        s_zero_pulse_ms = 0;
        float pps = ((float)pulses * 1000.0f) / (float)elapsed_ms;
        float speed_kmh = (pps / s_pulses_per_meter) * 3.6f;
        hud_state_set_vss_speed(speed_kmh);
    } else {
        s_zero_pulse_ms += elapsed_ms;
        hud_state_t snap;
        hud_state_get_snapshot(&snap);
        if (s_zero_pulse_ms > 10000 && snap.ble_gps_valid && snap.ble_gps_speed > 15.0f) {
            // Fault detected & fallback: VSS continuous 0 while BLE GPS > 15 km/h for > 10 seconds
            snap.speed_kmh = snap.ble_gps_speed;
            snap.speed_source = SPEED_SOURCE_BLE_GPS;
            hud_state_update(&snap);
        } else {
            hud_state_set_vss_speed(0.0f);
        }
    }
}
