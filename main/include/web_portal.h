/**
 * @file web_portal.h
 * @brief Wi-Fi SoftAP and embedded Web Configuration & OTA Portal for ESP32 Scooter Dashboard.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AP_SSID            "VarioHUD-Setup"
#define WIFI_AP_PASS            "" // Open network (no password)
#define WIFI_AP_MAX_CONN        4
#define WIFI_AP_CHANNEL         1

/**
 * @brief Initialize Wi-Fi SoftAP and launch embedded HTTP Web Server.
 * @return ESP_OK on success.
 */
esp_err_t web_portal_init(void);

/**
 * @brief Stop Web Server and Wi-Fi SoftAP.
 */
void web_portal_stop(void);

/**
 * @brief Check Wi-Fi SoftAP auto-sleep timeout.
 */
void web_portal_check_auto_sleep(void);

/**
 * @brief Register activity on Web Portal to reset sleep timer.
 */
void web_portal_activity_ping(void);

/**
 * @brief Check if Web Portal is running.
 */
bool web_portal_is_running(void);

#ifdef __cplusplus
}
#endif
