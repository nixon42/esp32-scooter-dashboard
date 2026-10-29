/**
 * @file chronos_packet.h
 * @brief Chronos BLE protocol packet assembly and decoding.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHRONOS_MAX_PACKET_LEN  512
#define CHRONOS_ICON_SIZE       288 // 48x48 1-bpp bitmap

typedef struct {
    uint8_t buffer[CHRONOS_MAX_PACKET_LEN];
    uint16_t expected_len;
    uint16_t received_len;
    bool in_progress;
} chronos_rx_buffer_t;

void chronos_packet_init(chronos_rx_buffer_t *rx_buf);
esp_err_t chronos_packet_feed(chronos_rx_buffer_t *rx_buf, const uint8_t *data, size_t len);
esp_err_t chronos_packet_process(const uint8_t *packet, size_t len);
size_t chronos_get_info_packet(uint8_t *out_buf);
size_t chronos_get_battery_packet(uint8_t *out_buf, uint8_t battery_pct, bool is_charging);
size_t chronos_get_request_phone_battery_packet(uint8_t *out_buf);
size_t chronos_get_sync_packet(uint8_t *out_buf);

#ifdef __cplusplus
}
#endif
