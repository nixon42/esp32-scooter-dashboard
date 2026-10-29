/**
 * @file chronos_packet.c
 * @brief Chronos BLE protocol packet assembly and decoding implementation.
 */

#include "chronos_packet.h"
#include "hud_state.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "CHRONOS_PKT";

// Icon assembly buffer for 3 chunks (96 bytes * 3 = 288 bytes)
static uint8_t s_icon_chunks[CHRONOS_ICON_SIZE];
static uint8_t s_icon_chunk_mask = 0;

void chronos_packet_init(chronos_rx_buffer_t *rx_buf)
{
    if (!rx_buf) return;
    memset(rx_buf, 0, sizeof(chronos_rx_buffer_t));
}

esp_err_t chronos_packet_feed(chronos_rx_buffer_t *rx_buf, const uint8_t *data, size_t len)
{
    if (!rx_buf || !data || len == 0) return ESP_ERR_INVALID_ARG;

    // Detect packet header: 0xAB or 0xEA with 0xFE or 0xFF marker
    if ((data[0] == 0xAB || data[0] == 0xEA) && len >= 4 && (data[3] == 0xFE || data[3] == 0xFF)) {
        rx_buf->expected_len = ((uint16_t)data[1] << 8) | data[2];
        rx_buf->expected_len += 3; // Length does not include prefix

        if (rx_buf->expected_len > CHRONOS_MAX_PACKET_LEN) {
            ESP_LOGW(TAG, "Packet expected length %d exceeds max buffer", rx_buf->expected_len);
            rx_buf->in_progress = false;
            return ESP_ERR_INVALID_SIZE;
        }

        size_t copy_len = (len < rx_buf->expected_len) ? len : rx_buf->expected_len;
        memcpy(rx_buf->buffer, data, copy_len);
        rx_buf->received_len = copy_len;
        rx_buf->in_progress = (rx_buf->received_len < rx_buf->expected_len);

        if (!rx_buf->in_progress) {
            return chronos_packet_process(rx_buf->buffer, rx_buf->received_len);
        }
        return ESP_ERR_NOT_FINISHED;
    }

    // Continuation chunk
    if (rx_buf->in_progress) {
        size_t offset = 20 + (data[0] * 19);
        if (offset < CHRONOS_MAX_PACKET_LEN && len > 1) {
            size_t payload_len = len - 1;
            if (offset + payload_len > CHRONOS_MAX_PACKET_LEN) {
                payload_len = CHRONOS_MAX_PACKET_LEN - offset;
            }
            memcpy(&rx_buf->buffer[offset], &data[1], payload_len);
            rx_buf->received_len = offset + payload_len;

            if (rx_buf->received_len >= rx_buf->expected_len) {
                rx_buf->in_progress = false;
                return chronos_packet_process(rx_buf->buffer, rx_buf->expected_len);
            }
            return ESP_ERR_NOT_FINISHED;
        }
    }

    return ESP_FAIL;
}

static void sanitize_ascii_string(char *dest, const uint8_t *src, size_t src_len, size_t dest_size)
{
    if (!dest || dest_size == 0) return;
    size_t d_idx = 0;
    for (size_t i = 0; i < src_len && src[i] != '\0' && d_idx < dest_size - 1; i++) {
        if (src[i] >= 32 && src[i] <= 126) {
            dest[d_idx++] = (char)src[i];
        }
    }
    dest[d_idx] = '\0';
}

static void parse_navigation_strings(const uint8_t *data, size_t len, hud_state_t *state)
{
    // String sequence starting at offset 12: title, duration, distance, eta, directions, speed
    size_t i = 12;
    int str_idx = 0;
    char temp[64];
    size_t t_idx = 0;

    // Reset GPS speed validity before unpacking current packet
    state->ble_gps_valid = false;

    while (i < len && str_idx < 6) {
        if (data[i] == '\0') {
            temp[t_idx] = '\0';
            switch (str_idx) {
                case 0: // Title
                    strncpy(state->nav_title, temp, sizeof(state->nav_title) - 1);
                    state->nav_title[sizeof(state->nav_title) - 1] = '\0';
                    break;
                case 1: // Duration
                    strncpy(state->nav_duration, temp, sizeof(state->nav_duration) - 1);
                    state->nav_duration[sizeof(state->nav_duration) - 1] = '\0';
                    break;
                case 2: // Distance
                    strncpy(state->nav_distance, temp, sizeof(state->nav_distance) - 1);
                    state->nav_distance[sizeof(state->nav_distance) - 1] = '\0';
                    break;
                case 3: // ETA
                    strncpy(state->nav_eta, temp, sizeof(state->nav_eta) - 1);
                    state->nav_eta[sizeof(state->nav_eta) - 1] = '\0';
                    break;
                case 4: // Directions
                    strncpy(state->nav_directions, temp, sizeof(state->nav_directions) - 1);
                    state->nav_directions[sizeof(state->nav_directions) - 1] = '\0';
                    break;
                case 5: { // Speed string (e.g. "45 km/h", "30 mph", or "45")
                    if (t_idx > 0) {
                        char *endptr = NULL;
                        float spd = strtof(temp, &endptr);
                        if (endptr != temp) {
                            while (*endptr == ' ') endptr++;
                            if (strncasecmp(endptr, "mph", 3) == 0) {
                                spd *= 1.609344f;
                            }
                            if (spd < 0.0f) spd = 0.0f;
                            state->ble_gps_speed = spd;
                            state->ble_gps_valid = true;
                            if (!state->vss_enabled) {
                                state->speed_kmh = spd;
                                state->speed_source = SPEED_SOURCE_BLE_GPS;
                            }
                        }
                    }
                    break;
                }
            }
            str_idx++;
            t_idx = 0;
        } else {
            if (t_idx < sizeof(temp) - 1) {
                temp[t_idx++] = (char)data[i];
            }
        }
        i++;
    }

    // If GPS speed was not provided or invalid, ensure state is reset when VSS disabled
    if (!state->ble_gps_valid && !state->vss_enabled) {
        state->speed_kmh = 0.0f;
        state->speed_source = SPEED_SOURCE_NONE;
    }

    // Fallback: If nav_distance is empty, extract distance to turn from title or directions
    if (state->nav_distance[0] == '\0') {
        const char *srcs[2] = { state->nav_title, state->nav_directions };
        for (int s = 0; s < 2; s++) {
            const char *p = srcs[s];
            if (!p || p[0] == '\0') continue;
            while (*p) {
                if (*p >= '0' && *p <= '9') {
                    const char *n_start = p;
                    while (*p && ((*p >= '0' && *p <= '9') || *p == '.' || *p == ',' || *p == ' ')) p++;
                    if (*p == 'k' || *p == 'K' || *p == 'm' || *p == 'M') {
                        const char *u_end = p;
                        if ((*p == 'k' || *p == 'K') && (*(p + 1) == 'm' || *(p + 1) == 'M')) {
                            u_end = p + 2;
                        } else if (*p == 'm' || *p == 'M') {
                            u_end = p + 1;
                        }
                        size_t dlen = u_end - n_start;
                        if (dlen > 0 && dlen < sizeof(state->nav_distance) - 1) {
                            strncpy(state->nav_distance, n_start, dlen);
                            state->nav_distance[dlen] = '\0';
                            break;
                        }
                    }
                } else {
                    p++;
                }
            }
            if (state->nav_distance[0] != '\0') break;
        }
    }
}

esp_err_t chronos_packet_process(const uint8_t *packet, size_t len)
{
    if (!packet || len < 5) return ESP_ERR_INVALID_ARG;

    hud_state_t snap;
    hud_state_get_snapshot(&snap);

    if (packet[0] == 0xAB) {
        uint8_t cmd = packet[4];

        switch (cmd) {
            case 0x72: { // Notification or Incoming Call
                if (len < 8) break;
                uint8_t icon = packet[6];
                uint8_t state = packet[7];

                if (icon == 0x01) { // Ringer Alert / Incoming Call
                    snap.notification_active = true;
                    snap.notif_app_id = icon;
                    strncpy(snap.notif_title, "PANGGILAN", sizeof(snap.notif_title));
                    size_t msg_len = len - 8;
                    if (msg_len >= sizeof(snap.notif_message)) msg_len = sizeof(snap.notif_message) - 1;
                    memcpy(snap.notif_message, &packet[8], msg_len);
                    snap.notif_message[msg_len] = '\0';
                    snap.notif_timestamp = (uint32_t)(esp_timer_get_time() / 1000ULL);
                    hud_state_update(&snap);
                    ESP_LOGI(TAG, "Incoming call: %s", snap.notif_message);
                } else if (icon == 0x02) { // Call Cancelled / Ended
                    snap.notification_active = false;
                    hud_state_update(&snap);
                    ESP_LOGI(TAG, "Call cancelled");
                } else if (state == 0x02) { // App Notification
                    snap.notification_active = true;
                    snap.notif_app_id = icon;

                    // Message text starts at index 8. Search for colon separator
                    const char *raw_text = (const char *)&packet[8];
                    size_t raw_len = len - 8;

                    // Strip generic "Notification:" or "Notification :" prefix from Chronos
                    if (raw_len >= 13 && strncasecmp(raw_text, "Notification:", 13) == 0) {
                        raw_text += 13;
                        raw_len -= 13;
                        while (raw_len > 0 && *raw_text == ' ') { raw_text++; raw_len--; }
                    } else if (raw_len >= 14 && strncasecmp(raw_text, "Notification :", 14) == 0) {
                        raw_text += 14;
                        raw_len -= 14;
                        while (raw_len > 0 && *raw_text == ' ') { raw_text++; raw_len--; }
                    }

                    // Look for separator (: or \n) within the first 35 characters
                    const char *sep = NULL;
                    size_t scan_len = (raw_len < 35) ? raw_len : 35;
                    for (size_t s = 0; s < scan_len; s++) {
                        if (raw_text[s] == ':' || raw_text[s] == '\n') {
                            sep = &raw_text[s];
                            break;
                        }
                    }

                    if (sep && (size_t)(sep - raw_text) < sizeof(snap.notif_title)) {
                        size_t tlen = (size_t)(sep - raw_text);
                        memcpy(snap.notif_title, raw_text, tlen);
                        snap.notif_title[tlen] = '\0';

                        size_t mlen = raw_len - tlen - 1;
                        const char *msg_ptr = sep + 1;
                        while (mlen > 0 && (*msg_ptr == ' ' || *msg_ptr == '\r' || *msg_ptr == '\n')) {
                            msg_ptr++;
                            mlen--;
                        }
                        if (mlen >= sizeof(snap.notif_message)) mlen = sizeof(snap.notif_message) - 1;
                        memcpy(snap.notif_message, msg_ptr, mlen);
                        snap.notif_message[mlen] = '\0';

                        // Check if title itself is "Discord" or "[Discord]"
                        if (strcasecmp(snap.notif_title, "Discord") == 0 ||
                            strcasecmp(snap.notif_title, "[Discord]") == 0) {
                            snap.notif_app_id = CHRONOS_APP_DISCORD;
                            // Check if message has a secondary "Sender: Msg" separator
                            const char *sub_sep = memchr(snap.notif_message, ':', (mlen < 30) ? mlen : 30);
                            if (sub_sep) {
                                size_t sub_tlen = (size_t)(sub_sep - snap.notif_message);
                                char sender[48];
                                if (sub_tlen < sizeof(sender)) {
                                    memcpy(sender, snap.notif_message, sub_tlen);
                                    sender[sub_tlen] = '\0';
                                    const char *sub_msg = sub_sep + 1;
                                    while (*sub_msg == ' ') sub_msg++;
                                    strncpy(snap.notif_title, sender, sizeof(snap.notif_title) - 1);
                                    snap.notif_title[sizeof(snap.notif_title) - 1] = '\0';
                                    strncpy(snap.notif_message, sub_msg, sizeof(snap.notif_message) - 1);
                                    snap.notif_message[sizeof(snap.notif_message) - 1] = '\0';
                                }
                            } else {
                                snap.notif_title[0] = '\0'; // Just message content
                            }
                        }
                    } else {
                        const char *default_title = "NOTIFIKASI";
                        switch (icon) {
                            case 0x03: default_title = "NOTIFIKASI"; break;
                            case 0x04: default_title = "EMAIL"; break;
                            case 0x08: default_title = "TWITTER"; break;
                            case 0x09: default_title = "INSTAGRAM"; break;
                            case 0x0A: default_title = "WHATSAPP"; break;
                            case 0x0B: default_title = "FACEBOOK"; break;
                            case 0x18: default_title = "TELEGRAM"; break;
                            case 0xC0: default_title = "CHRONOS"; break;
                            default:   default_title = "NOTIFIKASI"; break;
                        }
                        strncpy(snap.notif_title, default_title, sizeof(snap.notif_title) - 1);
                        snap.notif_title[sizeof(snap.notif_title) - 1] = '\0';
                        size_t mlen = raw_len;
                        if (mlen >= sizeof(snap.notif_message)) mlen = sizeof(snap.notif_message) - 1;
                        memcpy(snap.notif_message, raw_text, mlen);
                        snap.notif_message[mlen] = '\0';
                    }

                    // Check if raw_text, title, or message contains specific apps
                    if (strcasestr(raw_text, "maps") != NULL ||
                        strcasestr(snap.notif_title, "maps") != NULL ||
                        strcasestr(snap.notif_message, "maps") != NULL ||
                        strcasestr(raw_text, "gmaps") != NULL ||
                        strcasestr(snap.notif_title, "gmaps") != NULL ||
                        strcasestr(snap.notif_message, "gmaps") != NULL ||
                        strcasestr(raw_text, "petunjuk arah") != NULL ||
                        strcasestr(snap.notif_title, "petunjuk arah") != NULL ||
                        strcasestr(snap.notif_message, "petunjuk arah") != NULL) {
                        snap.notif_app_id = CHRONOS_APP_MAPS;
                        if (strcasecmp(snap.notif_title, "PESAN") == 0 ||
                            strcasecmp(snap.notif_title, "NOTIFIKASI") == 0 ||
                            snap.notif_title[0] == '\0') {
                            strncpy(snap.notif_title, "GMAPS", sizeof(snap.notif_title) - 1);
                        }
                    } else if (strcasestr(raw_text, "waze") != NULL ||
                               strcasestr(snap.notif_title, "waze") != NULL ||
                               strcasestr(snap.notif_message, "waze") != NULL) {
                        snap.notif_app_id = CHRONOS_APP_WAZE;
                        if (strcasecmp(snap.notif_title, "PESAN") == 0 ||
                            strcasecmp(snap.notif_title, "NOTIFIKASI") == 0 ||
                            snap.notif_title[0] == '\0') {
                            strncpy(snap.notif_title, "WAZE", sizeof(snap.notif_title) - 1);
                        }
                    } else if (strcasestr(raw_text, "discord") != NULL ||
                               strcasestr(snap.notif_title, "discord") != NULL ||
                               strcasestr(snap.notif_message, "discord") != NULL) {
                        snap.notif_app_id = CHRONOS_APP_DISCORD;
                    }

                    if (snap.nav_active && (snap.notif_app_id == CHRONOS_APP_MAPS || snap.notif_app_id == CHRONOS_APP_WAZE)) {
                        snap.notification_active = false;
                        ESP_LOGI(TAG, "Suppressed duplicate navigation notification during active Nav HUD.");
                        break;
                    }

                    snap.notif_timestamp = (uint32_t)(esp_timer_get_time() / 1000ULL);
                    hud_state_update(&snap);
                    ESP_LOGI(TAG, "Notification: app=0x%02X [%s] %s", snap.notif_app_id, snap.notif_title, snap.notif_message);
                }
                break;
            }

            case 0x7E: { // Daily / Current Weather
                if (len >= 8) {
                    int icon = packet[6] >> 4;
                    int sign = (packet[6] & 1) ? -1 : 1;
                    int temp = ((int)packet[7]) * sign;
                    snap.weather_current_temp = (int8_t)temp;
                    snap.weather_current_icon = (uint8_t)icon;
                    snap.weather_valid = true;
                    hud_state_update(&snap);
                    hud_state_save_weather_to_nvs();
                    ESP_LOGI(TAG, "Chronos current weather: %d°C, icon=%d", temp, icon);
                }
                break;
            }

            case 0x91: { // Phone Battery Response / Telemetry
                if (len >= 8) {
                    snap.phone_charging = (packet[6] != 0);
                    snap.phone_battery_pct = (packet[7] > 100) ? 100 : packet[7];
                    snap.phone_battery_valid = true;
                    hud_state_update(&snap);
                    ESP_LOGI(TAG, "Phone battery received: %d%% (charging=%d)",
                             snap.phone_battery_pct, snap.phone_charging);

                    // Low Phone Battery Alert: Trigger notification & alert once when <= 15% and not charging
                    static uint8_t s_last_alert_pbat = 100;
                    if (snap.phone_battery_pct <= 15 && !snap.phone_charging) {
                        if (s_last_alert_pbat > 15) {
                            snap.notification_active = true;
                            snap.notif_app_id = CHRONOS_APP_MESSAGE;
                            strncpy(snap.notif_title, "BATERAI HP", sizeof(snap.notif_title) - 1);
                            snprintf(snap.notif_message, sizeof(snap.notif_message) - 1, "Sisa %d%%! Segera isi daya.", snap.phone_battery_pct);
                            snap.notif_timestamp = (uint32_t)(esp_timer_get_time() / 1000ULL);
                            hud_state_update(&snap);
                            s_last_alert_pbat = snap.phone_battery_pct;
                        }
                    } else if (snap.phone_charging || snap.phone_battery_pct > 20) {
                        s_last_alert_pbat = 100;
                    }
                }
                break;
            }

            case 0x93: { // Time Synchronization
                if (len >= 14) {
                    struct tm timeinfo = {0};
                    timeinfo.tm_year = ((packet[7] << 8) | packet[8]) - 1900;
                    timeinfo.tm_mon  = packet[9] - 1;
                    timeinfo.tm_mday = packet[10];
                    timeinfo.tm_hour = packet[11];
                    timeinfo.tm_min  = packet[12];
                    timeinfo.tm_sec  = packet[13];

                    time_t t = mktime(&timeinfo);
                    if (t != (time_t)-1) {
                        struct timeval now = { .tv_sec = t, .tv_usec = 0 };
                        settimeofday(&now, NULL);
                        snap.time_synced = true;
                        hud_state_update(&snap);
                        hud_state_save_clock_time(t, true);
                        ESP_LOGI(TAG, "Time synced: %02d:%02d:%02d %02d/%02d/%04d",
                                 packet[11], packet[12], packet[13], packet[10], packet[9],
                                 ((packet[7] << 8) | packet[8]));
                    }
                }
                break;

            }

            case 0x9D: { // Music / Media Playback Info
                if (len >= 6) {
                    uint8_t sub = packet[5];
                    switch (sub) {
                        case 0x80: { // Media Playback State & App Info
                            if (len >= 7) {
                                uint8_t state = packet[6];
                                // State: 1 = Playing, 2 = Paused, 0 = Stopped/None
                                snap.music_active = (state == 1 || state == 3);
                                if (!snap.music_active) {
                                    snap.music_title[0] = '\0';
                                    snap.music_artist[0] = '\0';
                                }
                                if (len > 13) {
                                    char app_name[32] = {0};
                                    sanitize_ascii_string(app_name, &packet[13], len - 13, sizeof(app_name));
                                    ESP_LOGI(TAG, "Music app: '%s', playback state=%d (active=%d)",
                                             app_name, state, snap.music_active);
                                }
                            }
                            break;
                        }

                        case 0x81: { // Track Title (string starts at packet[7])
                            if (len > 7) {
                                sanitize_ascii_string(snap.music_title, &packet[7], len - 7, sizeof(snap.music_title));
                                if (strcasecmp(snap.music_title, "media") == 0 ||
                                    strncasecmp(snap.music_title, "media ", 6) == 0 ||
                                    strncasecmp(snap.music_title, "no media", 8) == 0 ||
                                    strcasecmp(snap.music_title, "none") == 0 ||
                                    strcasecmp(snap.music_title, "stopped") == 0 ||
                                    strcasecmp(snap.music_title, "idle") == 0) {
                                    snap.music_active = false;
                                    snap.music_title[0] = '\0';
                                    snap.music_artist[0] = '\0';
                                } else if (!snap.music_active) {
                                    // If playback state is not active, clear title
                                    snap.music_title[0] = '\0';
                                }
                                ESP_LOGI(TAG, "Music Title: '%s', active=%d", snap.music_title, snap.music_active);
                            }
                            break;
                        }

                        case 0x82: { // Artist Name (string starts at packet[7])
                            if (len > 7) {
                                sanitize_ascii_string(snap.music_artist, &packet[7], len - 7, sizeof(snap.music_artist));
                                if (!snap.music_active) {
                                    snap.music_artist[0] = '\0';
                                }
                                ESP_LOGI(TAG, "Music Artist: '%s'", snap.music_artist);
                            }
                            break;
                        }

                        default:
                            break;
                    }
                    hud_state_update(&snap);
                }
                break;
            }

            case 0xEE: { // Navigation Icon Chunk (48x48 1-bit bitmap in 3x96 chunks)
                if (len >= 107) {
                    uint8_t pos = packet[6]; // 0, 1, or 2
                    if (pos < 3) {
                        memcpy(&s_icon_chunks[pos * 96], &packet[11], 96);
                        s_icon_chunk_mask |= (1 << pos);

                        if (s_icon_chunk_mask == 0x07) { // All 3 chunks received
                            memcpy(snap.nav_icon_bmp, s_icon_chunks, CHRONOS_ICON_SIZE);
                            snap.nav_icon_valid = true;
                            s_icon_chunk_mask = 0;
                            hud_state_update(&snap);
                            ESP_LOGI(TAG, "Chronos 48x48 nav icon assembled successfully.");
                        }
                    }
                }
                break;
            }

            case 0xEF: { // Navigation Telemetry Packet
                if (len >= 6) {
                    uint8_t nav_state = packet[5];
                    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
                    static uint32_t s_nav_stop_request_ms = 0;

                    if (nav_state == 0x00 || nav_state == 0xFF) {
                        if (snap.nav_active) {
                            if (s_nav_stop_request_ms == 0) {
                                s_nav_stop_request_ms = now_ms;
                                ESP_LOGI(TAG, "Navigation stop requested (state=0x%02X), debouncing 5s...", nav_state);
                            } else if (now_ms - s_nav_stop_request_ms >= 5000) {
                                snap.nav_active = false;
                                snap.nav_icon_valid = false;
                                snap.ble_gps_valid = false;
                                s_nav_stop_request_ms = 0;
                                if (!snap.vss_enabled) {
                                    snap.speed_kmh = 0.0f;
                                    snap.speed_source = SPEED_SOURCE_NONE;
                                }
                                ESP_LOGI(TAG, "Navigation stopped/disabled after debounce (state=0x%02X).", nav_state);
                                hud_state_update(&snap);
                            }
                        } else {
                            s_nav_stop_request_ms = 0;
                        }
                    } else if (nav_state == 0x80) {
                        s_nav_stop_request_ms = 0;
                        snap.nav_active = true;
                        snap.nav_timestamp = now_ms;
                        parse_navigation_strings(packet, len, &snap);
                        ESP_LOGI(TAG, "Nav: %s | Dist: %s | Dir: %s | Spd: %.1f km/h",
                                 snap.nav_title, snap.nav_distance, snap.nav_directions, snap.ble_gps_speed);
                        hud_state_update(&snap);
                    }
                }
                break;
            }

            default:
                break;
        }
    } else if (packet[0] == 0xEA) {
        uint8_t cmd = packet[4];

        switch (cmd) {
            case 0x7E: { // Weather from Chronos
                if (len >= 6) {
                    uint8_t sub = packet[5];
                    if (sub == 0x01) { // City Name
                        if (len > 7) {
                            sanitize_ascii_string(snap.weather_city, &packet[7], len - 7, sizeof(snap.weather_city));
                            snap.weather_valid = true;
                            hud_state_update(&snap);
                            hud_state_save_weather_to_nvs();
                            ESP_LOGI(TAG, "Chronos Weather City: '%s'", snap.weather_city);
                        }
                    } else if (sub == 0x02) { // Hourly Forecast
                        if (len >= 8) {
                            uint8_t size = packet[6];
                            uint8_t start_hour = packet[7];
                            ESP_LOGI(TAG, "Chronos hourly forecast: count=%d, start_hour=%d", size, start_hour);

                            for (uint8_t z = 0; z < size; z++) {
                                size_t offset = 8 + (6 * z);
                                if (offset + 5 >= len) break;
                                uint8_t h_idx = (start_hour + z) % 24;

                                uint8_t b0 = packet[offset];
                                int icon = b0 >> 4;
                                int sign = (b0 & 1) ? -1 : 1;
                                int temp = ((int)packet[offset + 1]) * sign;
                                uint16_t wind = ((uint16_t)packet[offset + 2] << 8) | packet[offset + 3];
                                uint8_t hum = packet[offset + 4];
                                uint8_t uv = packet[offset + 5];

                                snap.hourly_forecast[h_idx].hour = h_idx;
                                snap.hourly_forecast[h_idx].temp = (int8_t)temp;
                                snap.hourly_forecast[h_idx].icon = (uint8_t)icon;
                                snap.hourly_forecast[h_idx].humidity = hum;
                                snap.hourly_forecast[h_idx].wind = wind;
                                snap.hourly_forecast[h_idx].uv = uv;
                                snap.hourly_forecast[h_idx].valid = true;

                                ESP_LOGD(TAG, "Forecast %02d:00 -> %d C, icon %d, hum %d%%, wind %d",
                                         h_idx, temp, icon, hum, wind);
                            }
                            snap.weather_valid = true;
                            hud_state_update(&snap);
                            hud_state_save_weather_to_nvs();
                        }
                    }
                }
                break;
            }

            case 0x7F: { // Weather Location (Lat, Lon, City, Region, Country)
                if (len >= 7 && (packet[3] == 0xFE || packet[3] == 0xFF)) {
                    uint8_t payload_len = packet[6];
                    const uint8_t *payload = &packet[7];
                    if (payload_len > 8 && (size_t)(7 + payload_len) <= len) {
                        size_t idx = 8;
                        // Read city (null terminated)
                        size_t c_start = idx;
                        while (idx < payload_len && payload[idx] != 0x00) idx++;
                        if (idx < payload_len) {
                            sanitize_ascii_string(snap.weather_city, &payload[c_start], idx - c_start, sizeof(snap.weather_city));
                            idx++; // skip null
                        }
                        // Read region (null terminated)
                        size_t r_start = idx;
                        while (idx < payload_len && payload[idx] != 0x00) idx++;
                        if (idx < payload_len) {
                            sanitize_ascii_string(snap.weather_region, &payload[r_start], idx - r_start, sizeof(snap.weather_region));
                        }
                        snap.weather_valid = true;
                        hud_state_update(&snap);
                        hud_state_save_weather_to_nvs();
                        ESP_LOGI(TAG, "Chronos Location: City='%s', Region='%s'",
                                 snap.weather_city, snap.weather_region);
                    }
                }
                break;
            }

            default:
                break;
        }
    }

    return ESP_OK;
}

size_t chronos_get_info_packet(uint8_t *out_buf)
{
    if (!out_buf) return 0;
    // Standard Chronos ESP32 info handshake response (CS_VERSION 1.9.1, screen CF_ESP32_240)
    static const uint8_t s_info[20] = {
        0xAB, 0x00, 0x11, 0xFF, 0x92, 0xC0, 1, 91, 0x00, 0xFB,
        0x1E, 0x40, 0xC0, 0x0E, 0x32, 0x28, 0x00, 0xE2, 0x00, 0x80
    };
    memcpy(out_buf, s_info, 20);
    return 20;
}

size_t chronos_get_battery_packet(uint8_t *out_buf, uint8_t battery_pct, bool is_charging)
{
    if (!out_buf) return 0;
    out_buf[0] = 0xAB;
    out_buf[1] = 0x00;
    out_buf[2] = 0x05;
    out_buf[3] = 0xFF;
    out_buf[4] = 0x91;
    out_buf[5] = 0x80;
    out_buf[6] = is_charging ? 0x01 : 0x00;
    out_buf[7] = (battery_pct > 100) ? 100 : battery_pct;
    return 8;
}

size_t chronos_get_request_phone_battery_packet(uint8_t *out_buf)
{
    if (!out_buf) return 0;
    static const uint8_t s_req[7] = { 0xAB, 0x00, 0x04, 0xFE, 0x91, 0x80, 0x01 };
    memcpy(out_buf, s_req, 7);
    return 7;
}

size_t chronos_get_sync_packet(uint8_t *out_buf)
{
    if (!out_buf) return 0;
    static const uint8_t s_sync[6] = { 0xAB, 0x00, 0x03, 0xFE, 0x23, 0x80 };
    memcpy(out_buf, s_sync, 6);
    return 6;
}
