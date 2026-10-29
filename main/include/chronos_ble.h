/**
 * @file chronos_ble.h
 * @brief Apache NimBLE GATT server for Chronos companion app on ESP32-C3.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NimBLE stack, GAP advertising, and Chronos GATT service.
 * @return ESP_OK on success.
 */
esp_err_t chronos_ble_init(void);

/**
 * @brief Send notification data over Chronos TX characteristic.
 * @param data Byte buffer to send.
 * @param len Length of byte buffer.
 * @return ESP_OK on success, ESP_ERR_* on failure.
 */
esp_err_t chronos_ble_send_notify(const uint8_t *data, size_t len);

/**
 * @brief Check if BLE client is connected.
 * @return true if connected.
 */
bool chronos_ble_is_connected(void);

/**
 * @brief Get Bluetooth LE MAC address formatted as string (e.g. "90:DA:72:86:06:4A").
 * @param buf Output buffer (minimum 18 bytes).
 * @param max_len Length of output buffer.
 * @return ESP_OK on success.
 */
esp_err_t chronos_ble_get_mac_str(char *buf, size_t max_len);

/**
 * @brief Request sync of settings, time, alarms, and weather from Chronos app.
 * @return ESP_OK on success.
 */
esp_err_t chronos_ble_request_sync(void);

#ifdef __cplusplus
}
#endif
