/**
 * @file hud_ui.c
 * @brief HUD UI Rendering Engine for Nokia 5110 LCD (PCD8544).
 * Target: ESP32-C3 SuperMini (Honda Vario 125 K60)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "pcd8544.h"
#include "hud_ui.h"
#include "font5x7.h"
#include "boot_logos.h"
#include "ldr_sensor.h"
#include "buzzer.h"

static const char *TAG = "HUD_UI";
static bool s_alert_cue_active_flag = false;

bool hud_ui_is_alert_cue_active(void)
{
    return s_alert_cue_active_flag;
}

// -----------------------------------------------------------------------------
// 1. Primitive Drawing Functions
// -----------------------------------------------------------------------------

void hud_ui_draw_pixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= HUD_SCREEN_WIDTH || y < 0 || y >= HUD_SCREEN_HEIGHT) {
        return; // Clip bounds safely
    }

    uint8_t *fb = pcd8544_get_framebuffer();
    if (!fb) return;

    uint16_t idx = (y / 8) * HUD_SCREEN_WIDTH + x;
    uint8_t bit = 1 << (y % 8);

    if (color == 1) {
        fb[idx] |= bit;      // Black
    } else if (color == 0) {
        fb[idx] &= ~bit;     // White / Clear
    } else {
        fb[idx] ^= bit;      // Invert
    }
}

void hud_ui_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t dx = abs(x1 - x0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = -abs(y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        hud_ui_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void hud_ui_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    hud_ui_draw_line(x, y, x + w - 1, y, color);
    hud_ui_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    hud_ui_draw_line(x, y, x, y + h - 1, color);
    hud_ui_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void hud_ui_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    for (int16_t cy = y; cy < y + h; cy++) {
        for (int16_t cx = x; cx < x + w; cx++) {
            hud_ui_draw_pixel(cx, cy, color);
        }
    }
}

void hud_ui_draw_char_5x7(int16_t x, int16_t y, char c, uint8_t color)
{
    uint8_t ascii = (uint8_t)c;
    if (ascii < 32 || ascii > 127) {
        ascii = ' ';
    }
    const uint8_t *glyph = font5x7[ascii - 32];

    for (int16_t col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int16_t row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                hud_ui_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void hud_ui_draw_string(int16_t x, int16_t y, const char *str, uint8_t color)
{
    if (!str) return;
    int16_t cur_x = x;
    int16_t cur_y = y;

    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 8;
        } else {
            hud_ui_draw_char_5x7(cur_x, cur_y, *str, color);
            cur_x += 6; // 5 pixels width + 1 pixel spacing
        }
        str++;
    }
}

// Procedural 14x22 segmented large digit renderer for high speed readability
void hud_ui_draw_large_digit(int16_t x, int16_t y, char digit, uint8_t color)
{
    if (digit == ' ') return;
    if (digit < '0' || digit > '9') {
        if (digit == '-') {
            hud_ui_fill_rect(x + 2, y + 10, 10, 3, color);
        }
        return;
    }

    uint8_t val = digit - '0';
    // 7-segment bitmask: A(0), B(1), C(2), D(3), E(4), F(5), G(6)
    static const uint8_t segs[10] = {
        0x3F, // 0: A B C D E F
        0x06, // 1: B C
        0x5B, // 2: A B D E G
        0x4F, // 3: A B C D G
        0x66, // 4: B C F G
        0x6D, // 5: A C D F G
        0x7D, // 6: A C D E F G
        0x07, // 7: A B C
        0x7F, // 8: A B C D E F G
        0x6F  // 9: A B C D F G
    };
    uint8_t mask = segs[val];

    // Segment thickness: 3px, width: 14px, height: 22px
    if (mask & (1 << 0)) hud_ui_fill_rect(x + 2, y + 0, 10, 3, color);        // Seg A (top)
    if (mask & (1 << 1)) hud_ui_fill_rect(x + 11, y + 2, 3, 9, color);        // Seg B (top-right)
    if (mask & (1 << 2)) hud_ui_fill_rect(x + 11, y + 11, 3, 9, color);       // Seg C (bottom-right)
    if (mask & (1 << 3)) hud_ui_fill_rect(x + 2, y + 19, 10, 3, color);       // Seg D (bottom)
    if (mask & (1 << 4)) hud_ui_fill_rect(x + 0, y + 11, 3, 9, color);        // Seg E (bottom-left)
    if (mask & (1 << 5)) hud_ui_fill_rect(x + 0, y + 2, 3, 9, color);         // Seg F (top-left)
    if (mask & (1 << 6)) hud_ui_fill_rect(x + 2, y + 9, 10, 3, color);        // Seg G (middle)
}

void hud_ui_blit_chronos_icon(const uint8_t *icon_bmp, int16_t dest_x, int16_t dest_y)
{
    if (!icon_bmp) return;

    for (int16_t iy = 0; iy < 48; iy++) {
        int16_t sy = dest_y + iy;
        if (sy < 0 || sy >= HUD_SCREEN_HEIGHT) {
            continue; // Vertical clipping
        }
        for (int16_t ix = 0; ix < 48; ix++) {
            int16_t sx = dest_x + ix;
            if (sx < 0 || sx >= HUD_SCREEN_WIDTH) {
                continue; // Horizontal clipping
            }
            uint16_t byte_idx = iy * 6 + (ix / 8);
            uint8_t bit_pos = 7 - (ix % 8);
            uint8_t px_on = (icon_bmp[byte_idx] >> bit_pos) & 0x01;
            hud_ui_draw_pixel(sx, sy, px_on);
        }
    }
}

// -----------------------------------------------------------------------------
// 2. Subsystem Rendering Components
// -----------------------------------------------------------------------------

void hud_ui_blit_chronos_icon_24x24(const uint8_t *icon_bmp, int16_t dest_x, int16_t dest_y)
{
    if (!icon_bmp) return;

    for (int16_t iy = 0; iy < 24; iy++) {
        int16_t sy = dest_y + iy;
        if (sy < 0 || sy >= HUD_SCREEN_HEIGHT) {
            continue;
        }
        int16_t src_y0 = iy * 2;
        int16_t src_y1 = src_y0 + 1;
        for (int16_t ix = 0; ix < 24; ix++) {
            int16_t sx = dest_x + ix;
            if (sx < 0 || sx >= HUD_SCREEN_WIDTH) {
                continue;
            }
            int16_t src_x0 = ix * 2;
            int16_t src_x1 = src_x0 + 1;

            // 2x2 downsample from 48x48 1-bit bitmap
            uint8_t p00 = (icon_bmp[src_y0 * 6 + (src_x0 / 8)] >> (7 - (src_x0 % 8))) & 0x01;
            uint8_t p01 = (icon_bmp[src_y0 * 6 + (src_x1 / 8)] >> (7 - (src_x1 % 8))) & 0x01;
            uint8_t p10 = (icon_bmp[src_y1 * 6 + (src_x0 / 8)] >> (7 - (src_x0 % 8))) & 0x01;
            uint8_t p11 = (icon_bmp[src_y1 * 6 + (src_x1 / 8)] >> (7 - (src_x1 % 8))) & 0x01;

            if (p00 | p01 | p10 | p11) {
                hud_ui_draw_pixel(sx, sy, 1);
            }
        }
    }
}

static void truncate_string_ellipsis(char *dest, const char *src, size_t max_len)
{
    if (!src || !dest || max_len == 0) return;
    size_t len = strlen(src);
    if (len <= max_len) {
        strncpy(dest, src, max_len);
        dest[len] = '\0';
    } else {
        if (max_len > 2) {
            strncpy(dest, src, max_len - 2);
            dest[max_len - 2] = '.';
            dest[max_len - 1] = '.';
            dest[max_len] = '\0';
        } else {
            dest[0] = '.';
            dest[1] = '\0';
        }
    }
}

static void get_marquee_ticker(char *dest, const char *src, size_t window_len, uint32_t speed_ms)
{
    if (!src || !dest || window_len == 0) return;
    size_t slen = strlen(src);
    if (slen <= window_len) {
        strncpy(dest, src, window_len);
        dest[slen] = '\0';
        return;
    }
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    size_t cycle = slen + 3; // 3 space padding separator between loop cycles
    size_t step = (now_ms / (speed_ms ? speed_ms : 450)) % cycle;
    for (size_t i = 0; i < window_len; i++) {
        size_t idx = (step + i) % cycle;
        if (idx < slen) {
            dest[i] = src[idx];
        } else {
            dest[i] = ' ';
        }
    }
    dest[window_len] = '\0';
}

static void hud_ui_draw_music_glyph(int16_t x, int16_t y, uint8_t color)
{
    for (int16_t i = 0; i < 5; i++) {
        uint8_t col = glyph_music[i];
        for (int16_t r = 0; r < 7; r++) {
            if (col & (1 << r)) {
                hud_ui_draw_pixel(x + i, y + r, color);
            }
        }
    }
}

void hud_ui_draw_ble_glyph(int16_t x, int16_t y, uint8_t color)
{
    for (int16_t col = 0; col < 5; col++) {
        uint8_t line = glyph_ble[col];
        for (int16_t row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                hud_ui_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

static void hud_ui_draw_nokia_battery(int16_t x, int16_t y, uint8_t pct, bool charging)
{
    // Outer battery shell: 11x7 px (x .. x+10, y .. y+6)
    // Terminal nipple at x+11, y+2..y+4 (3px high)
    hud_ui_draw_rect(x, y, 11, 7, 1);
    hud_ui_draw_line(x + 11, y + 2, x + 11, y + 4, 1);

    if (charging) {
        // 1. Distinct 4x7 Lightning Bolt beside the battery on the left (x - 6)
        int16_t lx = x - 6;
        hud_ui_draw_pixel(lx + 2, y + 0, 1);
        hud_ui_draw_pixel(lx + 1, y + 1, 1); hud_ui_draw_pixel(lx + 2, y + 1, 1);
        hud_ui_draw_pixel(lx + 0, y + 2, 1); hud_ui_draw_pixel(lx + 1, y + 2, 1); hud_ui_draw_pixel(lx + 2, y + 2, 1);
        hud_ui_draw_line(lx + 0, y + 3, lx + 3, y + 3, 1);
        hud_ui_draw_pixel(lx + 2, y + 4, 1); hud_ui_draw_pixel(lx + 3, y + 4, 1);
        hud_ui_draw_pixel(lx + 1, y + 5, 1); hud_ui_draw_pixel(lx + 2, y + 5, 1);
        hud_ui_draw_pixel(lx + 1, y + 6, 1);

        // 2. Iconic Nokia Sequential Filling Animation inside the battery
        // Every 300ms, cycles: 0 -> 1 -> 2 -> 3 bars
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        uint8_t step = (now_ms / 300) % 4;

        if (step >= 1) {
            hud_ui_fill_rect(x + 2, y + 1, 2, 5, 1); // Bar 1
        }
        if (step >= 2) {
            hud_ui_fill_rect(x + 5, y + 1, 2, 5, 1); // Bar 2
        }
        if (step >= 3) {
            hud_ui_fill_rect(x + 8, y + 1, 2, 5, 1); // Bar 3
        }
    } else if (pct < 15) {
        // Critical / Low Battery State (<15%):
        // Prominent Exclamation Mark (!) in the center of the battery shell
        // Stem (x+5, y+1..y+3), Gap (y+4), Dot (x+5, y+5)
        hud_ui_draw_line(x + 5, y + 1, x + 5, y + 3, 1);
        hud_ui_draw_pixel(x + 5, y + 5, 1);
    } else {
        // Normal Battery Levels: 3 solid 2x5 pixel bars
        // Low: 1 bar (15% - 44%)
        // Med: 2 bars (45% - 74%)
        // Full: 3 bars (>= 75%)
        if (pct >= 15) {
            hud_ui_fill_rect(x + 2, y + 1, 2, 5, 1); // Bar 1
        }
        if (pct >= 45) {
            hud_ui_fill_rect(x + 5, y + 1, 2, 5, 1); // Bar 2
        }
        if (pct >= 75) {
            hud_ui_fill_rect(x + 8, y + 1, 2, 5, 1); // Bar 3
        }
    }
}

void hud_ui_draw_global_header(const hud_state_t *state)
{
    // Divider line at Y=7 across all 84 columns
    hud_ui_draw_line(0, 7, 83, 7, 1);

    if (!state) return;

    // 1. Left: Scooter Aki Battery Voltage (x: 2..31, y: 0)
    // Only displayed when sensor is enabled and voltage is valid (no placeholders)
    if (state->battery_enabled && state->battery_voltage >= 5.0f) {
        char v_buf[8];
        snprintf(v_buf, sizeof(v_buf), "%4.1fV", state->battery_voltage);
        hud_ui_draw_string(2, 0, v_buf, 1);
    }

    // 2. Android Phone BLE Connection Indicator (Bluetooth Icon, x: 51..55, y: 0)
    // Displayed whenever connected to Android phone (spine at x=52)
    if (state->ble_connected) {
        hud_ui_draw_ble_glyph(50, 0, 1);
    }

    // 3. Right: Smartphone Battery Icon (Nokia 3310 Style, x: 71..81, y: 0)
    // Only drawn when phone is connected and reporting valid battery level
    if (state->ble_connected && state->phone_battery_valid) {
        hud_ui_draw_nokia_battery(71, 0, state->phone_battery_pct, state->phone_charging);
    }
}

void hud_ui_draw_status_bar(const hud_state_t *state)
{
    hud_ui_draw_global_header(state);
}

static const char *get_weather_desc(uint8_t icon)
{
    switch (icon) {
        case 0: return "C.Berawan"; // Partly cloudy
        case 1: return "Cerah";     // Sunny
        case 2: return "Salju";     // Snow
        case 3: return "Hujan";     // Rain
        case 4: return "Berawan";   // Cloudy
        case 5: return "Badai";     // Storm
        case 6: return "Angin";     // Wind
        case 7: return "Kabut";     // Haze/Fog
        default: return "Cerah";
    }
}

static inline bool is_clock_colon_visible(bool time_synced)
{
    if (time_synced) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (tv.tv_usec < 500000);
    } else {
        uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        return ((ms / 500) % 2) == 0;
    }
}

static void hud_ui_draw_seg_digit(int16_t x, int16_t y, char digit, uint8_t w, uint8_t h, uint8_t t, uint8_t color)
{
    if (digit < '0' || digit > '9') return;
    uint8_t val = digit - '0';
    static const uint8_t segs[10] = {
        0x3F, // 0: A B C D E F
        0x06, // 1: B C
        0x5B, // 2: A B D E G
        0x4F, // 3: A B C D G
        0x66, // 4: B C F G
        0x6D, // 5: A C D F G
        0x7D, // 6: A C D E F G
        0x07, // 7: A B C
        0x7F, // 8: A B C D E F G
        0x6F  // 9: A B C D F G
    };
    uint8_t mask = segs[val];
    uint8_t halfH = h / 2;

    // A (top)
    if (mask & (1 << 0)) hud_ui_fill_rect(x + t, y, w - 2 * t, t, color);
    // B (top-right)
    if (mask & (1 << 1)) hud_ui_fill_rect(x + w - t, y + t, t, halfH - t, color);
    // C (bottom-right)
    if (mask & (1 << 2)) hud_ui_fill_rect(x + w - t, y + halfH, t, h - halfH - t, color);
    // D (bottom)
    if (mask & (1 << 3)) hud_ui_fill_rect(x + t, y + h - t, w - 2 * t, t, color);
    // E (bottom-left)
    if (mask & (1 << 4)) hud_ui_fill_rect(x, y + halfH, t, h - halfH - t, color);
    // F (top-left)
    if (mask & (1 << 5)) hud_ui_fill_rect(x, y + t, t, halfH - t, color);
    // G (middle)
    if (mask & (1 << 6)) hud_ui_fill_rect(x + t, y + halfH - (t / 2), w - 2 * t, t, color);
}

// -----------------------------------------------------------------------------
// VARIANT 0: Nokia 3310 Classic Standby
// -----------------------------------------------------------------------------
static void draw_clock_style_3310(const hud_state_t *state, int hour, int min, int wday, int mday, int mon, int year)
{
    static const char *s_months[12] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};

    // A. Left Side: Signal Strength (Antenna + 4 bars)
    hud_ui_draw_pixel(2, 2, 1);
    hud_ui_draw_pixel(4, 2, 1);
    hud_ui_draw_line(2, 3, 4, 3, 1);
    hud_ui_draw_line(3, 4, 3, 16, 1); // Antenna stem

    uint8_t sig_bars = state->ble_connected ? 4 : 1;
    if (sig_bars >= 1) hud_ui_fill_rect(5, 13, 2, 4, 1);
    if (sig_bars >= 2) hud_ui_fill_rect(8, 10, 2, 7, 1);
    if (sig_bars >= 3) hud_ui_fill_rect(11, 7, 2, 10, 1);
    if (sig_bars >= 4) hud_ui_fill_rect(14, 4, 2, 13, 1);

    if (state->ble_connected) {
        hud_ui_draw_ble_glyph(63, 4, 1);
    }

    // B. Right Side: Battery Indicator (Vertical outline + 4 bars)
    hud_ui_draw_rect(70, 3, 10, 15, 1);
    hud_ui_fill_rect(73, 1, 4, 2, 1); // Terminal pip

    uint8_t bat_bars = 4;
    if (state->battery_enabled && state->battery_voltage > 5.0f) {
        if (state->battery_voltage >= 12.4f) bat_bars = 4;
        else if (state->battery_voltage >= 12.0f) bat_bars = 3;
        else if (state->battery_voltage >= 11.5f) bat_bars = 2;
        else bat_bars = 1;
    } else if (state->phone_battery_valid) {
        if (state->phone_battery_pct >= 75) bat_bars = 4;
        else if (state->phone_battery_pct >= 50) bat_bars = 3;
        else if (state->phone_battery_pct >= 25) bat_bars = 2;
        else bat_bars = 1;
    }
    if (bat_bars >= 1) hud_ui_fill_rect(72, 14, 6, 2, 1);
    if (bat_bars >= 2) hud_ui_fill_rect(72, 11, 6, 2, 1);
    if (bat_bars >= 3) hud_ui_fill_rect(72, 8, 6, 2, 1);
    if (bat_bars >= 4) hud_ui_fill_rect(72, 5, 6, 2, 1);

    // C. Center Top: Operator / Bike Name
    const char *op_name = "VARIO 125";
    int16_t op_x = (HUD_SCREEN_WIDTH - (int16_t)(strlen(op_name) * 6 - 1)) / 2;
    hud_ui_draw_string(op_x, 3, op_name, 1);
    for (int16_t x = 20; x <= 64; x += 2) {
        hud_ui_draw_pixel(x, 11, 1);
    }

    // D. Center: Medium 7-Segment Digital Clock (7x13 digits)
    char h1 = (hour / 10) + '0';
    char h2 = (hour % 10) + '0';
    char m1 = (min / 10) + '0';
    char m2 = (min % 10) + '0';

    hud_ui_draw_seg_digit(23, 15, h1, 7, 13, 2, 1);
    hud_ui_draw_seg_digit(32, 15, h2, 7, 13, 2, 1);

    if (is_clock_colon_visible(state->time_synced)) {
        hud_ui_fill_rect(41, 18, 2, 2, 1);
        hud_ui_fill_rect(41, 24, 2, 2, 1);
    }

    hud_ui_draw_seg_digit(45, 15, m1, 7, 13, 2, 1);
    hud_ui_draw_seg_digit(54, 15, m2, 7, 13, 2, 1);

    // E. Date below clock (y: 30)
    char date_buf[20];
    if (state->time_synced) {
        snprintf(date_buf, sizeof(date_buf), "%02d-%s-%02d", mday, s_months[mon % 12], year % 100);
    } else {
        snprintf(date_buf, sizeof(date_buf), "HONDA MOTOR");
    }
    int16_t dx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(date_buf) * 6 - 1)) / 2;
    hud_ui_draw_string(dx, 30, date_buf, 1);

    // F. Bottom Softkey Button: [ MENU ] or [ speed ]
    hud_ui_draw_rect(24, 39, 36, 8, 1);
    char btn_buf[16];
    if (state->speed_kmh >= 3.0f) {
        snprintf(btn_buf, sizeof(btn_buf), "%2.0f KM", state->speed_kmh);
    } else {
        snprintf(btn_buf, sizeof(btn_buf), "MENU");
    }
    int16_t bx = 24 + (36 - (int16_t)(strlen(btn_buf) * 6 - 1)) / 2;
    hud_ui_draw_string(bx, 40, btn_buf, 1);
}

// -----------------------------------------------------------------------------
// VARIANT 1: Big 7-Segment Digital (Current Default Layout)
// -----------------------------------------------------------------------------
static void draw_clock_style_big_digital(const hud_state_t *state, int hour, int min, int wday, int mday, int mon)
{
    hud_ui_draw_global_header(state);

    // A. Big Centered 7-Segment Clock (y: 10..31, 22px high)
    hud_ui_draw_large_digit(9, 10, (hour / 10) + '0', 1);
    hud_ui_draw_large_digit(25, 10, (hour % 10) + '0', 1);

    if (is_clock_colon_visible(state->time_synced)) {
        hud_ui_fill_rect(41, 15, 2, 2, 1);
        hud_ui_fill_rect(41, 22, 2, 2, 1);
    }

    hud_ui_draw_large_digit(45, 10, (min / 10) + '0', 1);
    hud_ui_draw_large_digit(61, 10, (min % 10) + '0', 1);

    // B. Middle Row: Date & Day (y: 33..39)
    static const char *s_days[7] = {"MIN", "SEN", "SEL", "RAB", "KAM", "JUM", "SAB"};
    static const char *s_months[12] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};
    char date_buf[24];
    if (state->time_synced) {
        snprintf(date_buf, sizeof(date_buf), "%s, %02d %s",
                 s_days[wday % 7], mday, s_months[mon % 12]);
    } else {
        snprintf(date_buf, sizeof(date_buf), "HONDA VARIO");
    }
    int16_t dx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(date_buf) * 6 - 1)) / 2;
    if (dx < 0) dx = 0;
    hud_ui_draw_string(dx, 33, date_buf, 1);

    // C. Bottom Row: Smart Telemetry or Music (y: 41..47)
    char bot_buf[32];
    if (state->speed_kmh >= 3.0f) {
        if (state->dht_enabled && state->dht_valid) {
            snprintf(bot_buf, sizeof(bot_buf), "%2.0f km/h  %2.0f\x7F", state->speed_kmh, state->temperature_c);
        } else if (state->weather_valid) {
            snprintf(bot_buf, sizeof(bot_buf), "%2.0f km/h  %2d\x7F", state->speed_kmh, state->weather_current_temp);
        } else {
            snprintf(bot_buf, sizeof(bot_buf), "%2.0f km/h", state->speed_kmh);
        }
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(bot_buf) * 6 - 1)) / 2;
        if (bx < 0) bx = 0;
        hud_ui_draw_string(bx, 41, bot_buf, 1);
    } else if (state->media_notif_enabled && state->music_active &&
               state->music_title[0] != '\0' &&
               strcasecmp(state->music_title, "media") != 0 &&
               strncasecmp(state->music_title, "media ", 6) != 0 &&
               strncasecmp(state->music_title, "no media", 8) != 0) {
        char t_crop[16];
        get_marquee_ticker(t_crop, state->music_title, 11, 450);
        hud_ui_draw_music_glyph(2, 41, 1);
        hud_ui_draw_string(8, 41, t_crop, 1);
    } else if (state->dht_enabled && state->dht_valid) {
        snprintf(bot_buf, sizeof(bot_buf), "%2.0f\x7F" "C  %2.0f%%", state->temperature_c, state->humidity_pct);
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(bot_buf) * 6 - 1)) / 2;
        if (bx < 0) bx = 0;
        hud_ui_draw_string(bx, 41, bot_buf, 1);
    } else if (state->weather_valid) {
        const char *cond = get_weather_desc(state->weather_current_icon);
        snprintf(bot_buf, sizeof(bot_buf), "%d\x7F" "C %s", state->weather_current_temp, cond);
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(bot_buf) * 6 - 1)) / 2;
        if (bx < 0) bx = 0;
        hud_ui_draw_string(bx, 41, bot_buf, 1);
    } else {
        snprintf(bot_buf, sizeof(bot_buf), "STANDBY");
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(bot_buf) * 6 - 1)) / 2;
        if (bx < 0) bx = 0;
        hud_ui_draw_string(bx, 41, bot_buf, 1);
    }
}

// -----------------------------------------------------------------------------
// VARIANT 2: Nokia 5110 Screensaver (Inverted Bar)
// -----------------------------------------------------------------------------
static void draw_clock_style_5110_bar(const hud_state_t *state, int hour, int min, int wday, int mday, int mon)
{
    static const char *s_days[7] = {"MIN", "SEN", "SEL", "RAB", "KAM", "JUM", "SAB"};
    static const char *s_months[12] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};

    // A. Top Status Header (y: 2..8)
    hud_ui_draw_rect(3, 3, 6, 5, 1);
    hud_ui_draw_pixel(4, 2, 1);
    hud_ui_draw_pixel(7, 2, 1);
    hud_ui_draw_line(5, 1, 6, 1, 1);

    hud_ui_draw_string(12, 2, "KUNCI", 1);

    if (state->ble_connected) {
        hud_ui_draw_ble_glyph(44, 2, 1);
    }

    char v_str[16];
    if (state->battery_enabled && state->battery_voltage > 5.0f) {
        snprintf(v_str, sizeof(v_str), "%4.1fV", state->battery_voltage);
    } else if (state->ble_connected && state->phone_battery_valid) {
        snprintf(v_str, sizeof(v_str), "%d%%", state->phone_battery_pct);
    } else if (state->weather_valid) {
        snprintf(v_str, sizeof(v_str), "%2d\x7F" "C", state->weather_current_temp);
    } else {
        snprintf(v_str, sizeof(v_str), "VARIO");
    }
    hud_ui_draw_string(52, 2, v_str, 1);

    // B. Solid Inverted Black Bar (y: 11..31, 21px height)
    hud_ui_fill_rect(0, 11, 84, 21, 1);

    char h1 = (hour / 10) + '0';
    char h2 = (hour % 10) + '0';
    char m1 = (min / 10) + '0';
    char m2 = (min % 10) + '0';

    hud_ui_draw_seg_digit(20, 15, h1, 8, 14, 2, 0);
    hud_ui_draw_seg_digit(30, 15, h2, 8, 14, 2, 0);

    if (is_clock_colon_visible(state->time_synced)) {
        hud_ui_fill_rect(40, 18, 2, 2, 0);
        hud_ui_fill_rect(40, 24, 2, 2, 0);
    }

    hud_ui_draw_seg_digit(44, 15, m1, 8, 14, 2, 0);
    hud_ui_draw_seg_digit(54, 15, m2, 8, 14, 2, 0);

    // C. Bottom Area (y: 34..47)
    char date_buf[24];
    if (state->time_synced) {
        snprintf(date_buf, sizeof(date_buf), "%s, %02d %s",
                 s_days[wday % 7], mday, s_months[mon % 12]);
    } else {
        snprintf(date_buf, sizeof(date_buf), "HONDA VARIO 125");
    }
    int16_t dx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(date_buf) * 6 - 1)) / 2;
    hud_ui_draw_string(dx, 34, date_buf, 1);

    if (state->speed_kmh >= 3.0f) {
        char spd_buf[16];
        snprintf(spd_buf, sizeof(spd_buf), "%2.0f km/h", state->speed_kmh);
        int16_t sx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(spd_buf) * 6 - 1)) / 2;
        hud_ui_draw_string(sx, 41, spd_buf, 1);
    } else {
        for (int16_t x = 4; x <= 80; x += 3) {
            hud_ui_draw_pixel(x, 44, 1);
        }
    }
}

// -----------------------------------------------------------------------------
// VARIANT 3: Nokia Analogue Clock Face
// -----------------------------------------------------------------------------
static void draw_clock_style_analog(const hud_state_t *state, int hour, int min, int sec, int wday, int mday, int mon)
{
    static const char *s_days[7] = {"MIN", "SEN", "SEL", "RAB", "KAM", "JUM", "SAB"};
    static const char *s_months[12] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};

    const int16_t cx = 23;
    const int16_t cy = 24;
    const int16_t r  = 18;

    // A. Round Dial Circle
    for (int a = 0; a < 360; a += 5) {
        float rad = (float)a * 0.0174532925f;
        int16_t px = (int16_t)roundf((float)cx + (float)r * cosf(rad));
        int16_t py = (int16_t)roundf((float)cy + (float)r * sinf(rad));
        hud_ui_draw_pixel(px, py, 1);
    }

    // 4 Hour Ticks (12, 3, 6, 9)
    hud_ui_fill_rect(cx - 1, cy - r + 1, 3, 2, 1); // 12
    hud_ui_fill_rect(cx + r - 3, cy - 1, 2, 3, 1); // 3
    hud_ui_fill_rect(cx - 1, cy + r - 3, 3, 2, 1); // 6
    hud_ui_fill_rect(cx - r + 1, cy - 1, 2, 3, 1); // 9

    // Center pivot
    hud_ui_fill_rect(cx - 1, cy - 1, 3, 3, 1);

    // Clock Hands
    float hours = (float)(hour % 12) + (float)min / 60.0f;
    float minutes = (float)min + (float)sec / 60.0f;

    float hr_ang = (hours * 30.0f - 90.0f) * 0.0174532925f;
    float min_ang = (minutes * 6.0f - 90.0f) * 0.0174532925f;

    // Hour hand (thick, length 8)
    int16_t hx = (int16_t)roundf((float)cx + 8.0f * cosf(hr_ang));
    int16_t hy = (int16_t)roundf((float)cy + 8.0f * sinf(hr_ang));
    hud_ui_draw_line(cx, cy, hx, hy, 1);
    hud_ui_draw_line(cx + 1, cy, hx + 1, hy, 1);
    hud_ui_draw_line(cx, cy + 1, hx, hy + 1, 1);

    // Minute hand (length 13)
    int16_t mx = (int16_t)roundf((float)cx + 13.0f * cosf(min_ang));
    int16_t my = (int16_t)roundf((float)cy + 13.0f * sinf(min_ang));
    hud_ui_draw_line(cx, cy, mx, my, 1);

    // B. Vertical Dotted Separator at x=45
    for (int16_t y = 3; y < 45; y += 2) {
        hud_ui_draw_pixel(45, y, 1);
    }

    // C. Right Information Panel (x: 48..83)
    char time_str[10];
    char sep = is_clock_colon_visible(state->time_synced) ? ':' : ' ';
    snprintf(time_str, sizeof(time_str), "%02d%c%02d", hour, sep, min);
    hud_ui_draw_string(49, 3, time_str, 1);

    if (state->ble_connected) {
        hud_ui_draw_ble_glyph(79, 3, 1);
    }

    hud_ui_draw_line(47, 11, 83, 11, 1);

    char day_str[10];
    snprintf(day_str, sizeof(day_str), "%s", s_days[wday % 7]);
    hud_ui_draw_string(49, 14, day_str, 1);

    char date_str[12];
    snprintf(date_str, sizeof(date_str), "%02d %s", mday, s_months[mon % 12]);
    hud_ui_draw_string(47, 22, date_str, 1);

    hud_ui_draw_line(47, 30, 83, 30, 1);

    if (state->speed_kmh >= 3.0f) {
        char spd_str[12];
        snprintf(spd_str, sizeof(spd_str), "%2.0f KM", state->speed_kmh);
        hud_ui_draw_string(48, 33, spd_str, 1);
        if (state->dht_enabled && state->dht_valid) {
            char t_buf[10];
            snprintf(t_buf, sizeof(t_buf), "%2.0f\x7F", state->temperature_c);
            hud_ui_draw_string(48, 41, t_buf, 1);
        }
    } else {
        char v_str[12];
        if (state->battery_enabled && state->battery_voltage > 5.0f) {
            snprintf(v_str, sizeof(v_str), "%4.1fV", state->battery_voltage);
        } else if (state->ble_connected && state->phone_battery_valid) {
            snprintf(v_str, sizeof(v_str), "%d%%", state->phone_battery_pct);
        } else {
            snprintf(v_str, sizeof(v_str), "VARIO");
        }
        hud_ui_draw_string(48, 33, v_str, 1);

        char t_str[12];
        if (state->dht_enabled && state->dht_valid) {
            snprintf(t_str, sizeof(t_str), "%2.0f\x7F", state->temperature_c);
        } else if (state->weather_valid) {
            snprintf(t_str, sizeof(t_str), "%2d\x7F", state->weather_current_temp);
        } else {
            snprintf(t_str, sizeof(t_str), "HONDA");
        }
        hud_ui_draw_string(48, 41, t_str, 1);
    }
}

// -----------------------------------------------------------------------------
// VARIANT 4: Nokia 8210 Executive Compact
// -----------------------------------------------------------------------------
static void draw_clock_style_8210(const hud_state_t *state, int hour, int min, int wday, int mday, int mon)
{
    static const char *s_days[7] = {"MIN", "SEN", "SEL", "RAB", "KAM", "JUM", "SAB"};
    static const char *s_months[12] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};

    // A. Header (y: 1..8): Aki Voltage on left (or Day/Date if disabled), HP Battery or BLE on right
    char v_str[16];
    if (state->battery_enabled && state->battery_voltage > 5.0f) {
        snprintf(v_str, sizeof(v_str), "%4.1fV", state->battery_voltage);
    } else if (state->time_synced) {
        snprintf(v_str, sizeof(v_str), "%s %02d", s_days[wday % 7], mday);
    } else {
        snprintf(v_str, sizeof(v_str), "VARIO");
    }
    hud_ui_draw_string(2, 2, v_str, 1);

    hud_ui_draw_line(42, 1, 42, 8, 1);

    char r_str[16];
    if (state->ble_connected) {
        hud_ui_draw_ble_glyph(44, 2, 1);
        snprintf(r_str, sizeof(r_str), "%d%%", state->phone_battery_pct);
        hud_ui_draw_string(50, 2, r_str, 1);
    } else {
        snprintf(r_str, sizeof(r_str), "BLE OFF");
        hud_ui_draw_string(45, 2, r_str, 1);
    }

    hud_ui_draw_line(0, 9, 83, 9, 1);

    // B. Middle Band (y: 12..31)
    // Left Day Badge (x: 2..19, y: 13..29)
    hud_ui_draw_rect(2, 13, 18, 17, 1);
    hud_ui_draw_string(3, 14, s_days[wday % 7], 1);
    char day_num[6];
    snprintf(day_num, sizeof(day_num), "%02d", mday);
    hud_ui_draw_string(5, 22, day_num, 1);

    // Center Clock (8x15 digits)
    char h1 = (hour / 10) + '0';
    char h2 = (hour % 10) + '0';
    char m1 = (min / 10) + '0';
    char m2 = (min % 10) + '0';

    hud_ui_draw_seg_digit(23, 14, h1, 8, 15, 2, 1);
    hud_ui_draw_seg_digit(32, 14, h2, 8, 15, 2, 1);

    if (is_clock_colon_visible(state->time_synced)) {
        hud_ui_fill_rect(42, 17, 2, 2, 1);
        hud_ui_fill_rect(42, 23, 2, 2, 1);
    }

    hud_ui_draw_seg_digit(46, 14, m1, 8, 15, 2, 1);
    hud_ui_draw_seg_digit(55, 14, m2, 8, 15, 2, 1);

    // Right Month Badge (x: 66..82, y: 13..29)
    hud_ui_draw_rect(66, 13, 16, 17, 1);
    const char *m_name = s_months[mon % 12];
    char m_top[4] = { m_name[0], '\0' };
    char m_bot[4] = { m_name[1], m_name[2], '\0' };
    hud_ui_draw_string(72, 14, m_top, 1);
    hud_ui_draw_string(69, 22, m_bot, 1);

    hud_ui_draw_line(0, 32, 83, 32, 1);

    // C. Bottom Area (y: 35..45)
    if (state->speed_kmh >= 3.0f) {
        char spd_buf[24];
        if (state->dht_enabled && state->dht_valid) {
            snprintf(spd_buf, sizeof(spd_buf), "%2.0f km/h %2.0f\x7F", state->speed_kmh, state->temperature_c);
        } else {
            snprintf(spd_buf, sizeof(spd_buf), "%2.0f km/h", state->speed_kmh);
        }
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(spd_buf) * 6 - 1)) / 2;
        hud_ui_draw_string(bx, 35, spd_buf, 1);
    } else if (state->media_notif_enabled && state->music_active && state->music_title[0] != '\0') {
        char t_crop[16];
        get_marquee_ticker(t_crop, state->music_title, 11, 450);
        hud_ui_draw_music_glyph(2, 35, 1);
        hud_ui_draw_string(8, 35, t_crop, 1);
    } else if (state->dht_enabled && state->dht_valid) {
        char clim_buf[24];
        snprintf(clim_buf, sizeof(clim_buf), "%2.0f\x7F" "C %2.0f%%", state->temperature_c, state->humidity_pct);
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(clim_buf) * 6 - 1)) / 2;
        hud_ui_draw_string(bx, 35, clim_buf, 1);
    } else {
        const char *t = "VARIO 125";
        int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(t) * 6 - 1)) / 2;
        hud_ui_draw_string(bx, 35, t, 1);
    }

    for (int16_t x = 2; x <= 82; x += 2) {
        hud_ui_draw_pixel(x, 45, 1);
    }
}

static void draw_clock_standby_page(const hud_state_t *state)
{
    int hour = 12, min = 0, sec = 0, wday = 1, mday = 6, mon = 9, year = 2026;
    if (state->time_synced) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        hour = timeinfo.tm_hour;
        min = timeinfo.tm_min;
        sec = timeinfo.tm_sec;
        wday = timeinfo.tm_wday;
        mday = timeinfo.tm_mday;
        mon = timeinfo.tm_mon;
        year = timeinfo.tm_year + 1900;
    }

    switch (state->clock_style) {
        case CLOCK_STYLE_NOKIA_3310:
            draw_clock_style_3310(state, hour, min, wday, mday, mon, year);
            break;
        case CLOCK_STYLE_NOKIA_5110_BAR:
            draw_clock_style_5110_bar(state, hour, min, wday, mday, mon);
            break;
        case CLOCK_STYLE_ANALOG:
            draw_clock_style_analog(state, hour, min, sec, wday, mday, mon);
            break;
        case CLOCK_STYLE_NOKIA_8210:
            draw_clock_style_8210(state, hour, min, wday, mday, mon);
            break;
        case CLOCK_STYLE_BIG_DIGITAL:
        default:
            draw_clock_style_big_digital(state, hour, min, wday, mday, mon);
            break;
    }
}

// -----------------------------------------------------------------------------
// 16x16 Monochrome Weather Icons
// -----------------------------------------------------------------------------
static const uint16_t icon_weather_sun[16] = {
    0b0000000110000000,
    0b0000000110000000,
    0b0010000000000100,
    0b0001001111001000,
    0b0000111111110000,
    0b0000111111110000,
    0b0110111111110110,
    0b0110111111110110,
    0b0000111111110000,
    0b0000111111110000,
    0b0001001111001000,
    0b0010000000000100,
    0b0000000110000000,
    0b0000000110000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_partly[16] = {
    0b0010001000000000,
    0b0001010000000000,
    0b0111111100000000,
    0b0001011110000000,
    0b0010011111001100,
    0b0000111111111110,
    0b0001111111111111,
    0b0011111111111111,
    0b0011111111111111,
    0b0001111111111110,
    0b0000111111111100,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_cloud[16] = {
    0b0000000000000000,
    0b0000011100000000,
    0b0000111110011100,
    0b0001111111111110,
    0b0011111111111111,
    0b0111111111111111,
    0b0111111111111111,
    0b0111111111111111,
    0b0011111111111110,
    0b0001111111111100,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_rain[16] = {
    0b0000011100000000,
    0b0000111110011000,
    0b0001111111111110,
    0b0011111111111111,
    0b0111111111111111,
    0b0111111111111111,
    0b0011111111111110,
    0b0001111111111100,
    0b0000000000000000,
    0b0010001000100000,
    0b0100010001000000,
    0b0000000000000000,
    0b0001000100010000,
    0b0010001000100000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_storm[16] = {
    0b0000011100000000,
    0b0000111110011000,
    0b0001111111111110,
    0b0011111111111111,
    0b0111111111111111,
    0b0011111111111110,
    0b0000011100000000,
    0b0000110000000000,
    0b0001111100000000,
    0b0000011000000000,
    0b0000110000000000,
    0b0000100000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_fog[16] = {
    0b0000000000000000,
    0b0111111111111110,
    0b0000000000000000,
    0b0001111111111000,
    0b0000000000000000,
    0b0111111111111110,
    0b0000000000000000,
    0b0011111111111100,
    0b0000000000000000,
    0b0111111111111110,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_snow[16] = {
    0b0000000100000000,
    0b0100010101000100,
    0b0010010101001000,
    0b0001001100100000,
    0b0000111111000000,
    0b0111111111111110,
    0b0000111111000000,
    0b0001001100100000,
    0b0010010101001000,
    0b0100010101000100,
    0b0000000100000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_wind[16] = {
    0b0000000000000000,
    0b0000000001110000,
    0b0111111111001000,
    0b0000000001110000,
    0b0000000000000000,
    0b0000001110000000,
    0b1111111001000000,
    0b0000001110000000,
    0b0000000000000000,
    0b0011111111110000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static const uint16_t icon_weather_moon[16] = {
    0b0000000001000000,
    0b0000000111000000,
    0b0000001111000000,
    0b0000011110000000,
    0b0000111110000000,
    0b0001111100000000,
    0b0011111100000000,
    0b0011111100000000,
    0b0011111100000000,
    0b0001111100000000,
    0b0000111110000000,
    0b0000011110000000,
    0b0000001111000000,
    0b0000000111000000,
    0b0000000001000000,
    0b0000000000000000
};

static const uint16_t icon_weather_partly_night[16] = {
    0b0000000010000000,
    0b0000001110000000,
    0b0000011110000000,
    0b0000111100000000,
    0b0001111100011000,
    0b0011111001111110,
    0b0011111111111111,
    0b0111111111111111,
    0b0111111111111111,
    0b0011111111111110,
    0b0001111111111100,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

static void hud_ui_draw_weather_icon_16x16(int16_t x, int16_t y, uint8_t icon, bool is_night)
{
    const uint16_t *bmp = is_night ? icon_weather_moon : icon_weather_sun;
    switch (icon) {
        case 0: bmp = is_night ? icon_weather_partly_night : icon_weather_partly; break;
        case 1: bmp = is_night ? icon_weather_moon : icon_weather_sun; break;
        case 2: bmp = icon_weather_snow; break;
        case 3: bmp = icon_weather_rain; break;
        case 4: bmp = icon_weather_cloud; break;
        case 5: bmp = icon_weather_storm; break;
        case 6: bmp = icon_weather_wind; break;
        case 7: bmp = icon_weather_fog; break;
        default: bmp = is_night ? icon_weather_moon : icon_weather_sun; break;
    }
    for (int r = 0; r < 16; r++) {
        uint16_t row = bmp[r];
        for (int c = 0; c < 16; c++) {
            if (row & (1 << (15 - c))) {
                hud_ui_draw_pixel(x + c, y + r, 1);
            }
        }
    }
}

static void draw_weather_forecast_page(const hud_state_t *state)
{
    // A. City / Location Header (y: 1..7)
    char city_str[32];
    if (state->weather_city[0] != '\0') {
        truncate_string_ellipsis(city_str, state->weather_city, 13);
    } else if (state->weather_region[0] != '\0') {
        truncate_string_ellipsis(city_str, state->weather_region, 13);
    } else {
        snprintf(city_str, sizeof(city_str), "INFO CUACA");
    }
    int16_t cx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(city_str) * 6 - 1)) / 2;
    if (cx < 0) cx = 0;
    hud_ui_draw_string(cx, 1, city_str, 1);

    // Retro dotted separator line at y: 10
    for (int16_t x = 4; x < 80; x += 2) {
        hud_ui_draw_pixel(x, 10, 1);
    }

    // B. Dedicated 1-Hour Ahead Forecast in Spacious Retro Nokia Box (y: 13..46 - 34px height)
    hud_ui_draw_rect(2, 13, 80, 34, 1);

    chronos_hourly_forecast_t fc;
    bool has_fc = hud_state_get_forecast_1h(state, &fc);
    uint8_t fc_icon = has_fc ? fc.icon : state->weather_current_icon;
    int8_t fc_temp = has_fc ? fc.temp : state->weather_current_temp;
    const char *fc_cond = get_weather_desc(fc_icon);

    // Day/Night determination based on forecast target hour
    int target_hour = 12;
    if (has_fc) {
        target_hour = fc.hour;
    } else if (state->time_synced) {
        time_t now;
        time(&now);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        target_hour = (tm_now.tm_hour + 1) % 24;
    }
    bool is_night = (target_hour >= 18 || target_hour < 6);

    // Weather Icon on Left (16x16 vertically centered in box at y: 22..37, x: 6..21)
    hud_ui_draw_weather_icon_16x16(6, 22, fc_icon, is_night);

    // Dotted vertical divider line at x: 25 inside box (y: 16..43)
    for (int16_t dy = 16; dy < 44; dy += 2) {
        hud_ui_draw_pixel(25, dy, 1);
    }

    // Forecast Details on Right:
    // Line 1 (y: 16): +1 JAM
    hud_ui_draw_string(29, 16, "+1 JAM", 1);

    // Line 2 (y: 25): Temperature
    char l2[20];
    snprintf(l2, sizeof(l2), "%d\x7F" "C", fc_temp);
    hud_ui_draw_string(29, 25, l2, 1);

    // Line 3 (y: 35): Condition Name
    char l3[20];
    truncate_string_ellipsis(l3, fc_cond, 8);
    hud_ui_draw_string(29, 35, l3, 1);
}

static bool is_media_active(const hud_state_t *state)
{
    if (!state || !state->media_notif_enabled || !state->music_active) return false;
    if (state->music_title[0] == '\0') return false;
    if (strcasecmp(state->music_title, "media") == 0 ||
        strncasecmp(state->music_title, "media ", 6) == 0 ||
        strncasecmp(state->music_title, "no media", 8) == 0) {
        return false;
    }
    return true;
}


static const uint8_t icon_sensor_drop[8] = {
    0b00010000, // ...#....
    0b00111000, // ..###...
    0b00111000, // ..###...
    0b01111100, // .#####..
    0b01111100, // .#####..
    0b01111100, // .#####..
    0b00111000, // ..###...
    0b00000000  // ........
};

static const uint8_t icon_sensor_sun[8] = {
    0b00010000, // ...#....
    0b01000100, // .#...#..
    0b00111000, // ..###...
    0b11111110, // #######.
    0b00111000, // ..###...
    0b01000100, // .#...#..
    0b00010000, // ...#....
    0b00000000  // ........
};

static void hud_ui_draw_icon_8x8(int16_t x, int16_t y, const uint8_t *bmp)
{
    if (!bmp) return;
    for (int16_t r = 0; r < 8; r++) {
        uint8_t row = bmp[r];
        for (int16_t c = 0; c < 8; c++) {
            if (row & (1 << (7 - c))) {
                hud_ui_draw_pixel(x + c, y + r, 1);
            }
        }
    }
}

static const uint8_t icon_sensor_thermo_hero[10] = {
    0b00111000, // ..###...
    0b00101000, // ..#.#...
    0b00101000, // ..#.#...
    0b00111000, // ..###...
    0b00111000, // ..###...
    0b00111000, // ..###...
    0b01111100, // .#####..
    0b01111100, // .#####..
    0b01111100, // .#####..
    0b00111000  // ..###...
};

static void hud_ui_draw_icon_8x10(int16_t x, int16_t y, const uint8_t *bmp)
{
    if (!bmp) return;
    for (int16_t r = 0; r < 10; r++) {
        uint8_t row = bmp[r];
        for (int16_t c = 0; c < 8; c++) {
            if (row & (1 << (7 - c))) {
                hud_ui_draw_pixel(x + c, y + r, 1);
            }
        }
    }
}

static void draw_sensor_climate_page(const hud_state_t *state)
{
    // If BOTH sensors are disabled, skip screen sensor
    if (!state->dht_enabled && !state->ldr_enabled) {
        draw_clock_standby_page(state);
        return;
    }

    // =========================================================================
    // CASE 1: BOTH DHT & LDR ENABLED (3 Rows)
    // =========================================================================
    if (state->dht_enabled && state->ldr_enabled) {
        // --- Baris 1: Suhu (y: 1..15) ---
        char val_temp[16];
        uint8_t temp_fill = 0;
        if (state->dht_valid) {
            snprintf(val_temp, sizeof(val_temp), "%4.1f\x7F" "C", state->temperature_c);
            float t = state->temperature_c;
            if (t < 0.0f) t = 0.0f;
            if (t > 50.0f) t = 50.0f;
            temp_fill = (uint8_t)((t * 62.0f) / 50.0f);
            if (temp_fill > 62) temp_fill = 62;
        } else {
            snprintf(val_temp, sizeof(val_temp), "MEMBACA...");
        }

        hud_ui_draw_icon_8x10(4, 2, icon_sensor_thermo_hero);
        if (!state->dht_valid) {
            hud_ui_draw_string(16, 3, "Suhu", 1);
            hud_ui_draw_string(44, 3, val_temp, 1);
        } else {
            hud_ui_draw_string(16, 3, "Suhu", 1);
            hud_ui_draw_pixel(41, 6, 1);
            hud_ui_draw_pixel(43, 6, 1);
            hud_ui_draw_pixel(45, 6, 1);
            hud_ui_draw_string(47, 3, val_temp, 1);
        }
        hud_ui_draw_rect(15, 11, 66, 4, 1);
        if (temp_fill > 0) {
            hud_ui_fill_rect(17, 12, temp_fill, 2, 1);
        }
        for (int16_t x = 4; x < 80; x += 2) {
            hud_ui_draw_pixel(x, 16, 1);
        }

        // --- Baris 2: Kelembapan (y: 18..31) ---
        char val_hum[16];
        uint8_t hum_fill = 0;
        if (state->dht_valid) {
            snprintf(val_hum, sizeof(val_hum), "%2.0f %%", state->humidity_pct);
            float h = state->humidity_pct;
            if (h < 0.0f) h = 0.0f;
            if (h > 100.0f) h = 100.0f;
            hum_fill = (uint8_t)((h * 62.0f) / 100.0f);
            if (hum_fill > 62) hum_fill = 62;
        } else {
            snprintf(val_hum, sizeof(val_hum), "-- %%");
        }

        hud_ui_draw_icon_8x8(4, 18, icon_sensor_drop);
        hud_ui_draw_string(16, 19, "Lembap", 1);
        hud_ui_draw_pixel(52, 22, 1);
        hud_ui_draw_pixel(54, 22, 1);
        hud_ui_draw_pixel(56, 22, 1);
        hud_ui_draw_string(58, 19, val_hum, 1);
        hud_ui_draw_rect(15, 27, 66, 4, 1);
        if (hum_fill > 0) {
            hud_ui_fill_rect(17, 28, hum_fill, 2, 1);
        }
        for (int16_t x = 4; x < 80; x += 2) {
            hud_ui_draw_pixel(x, 32, 1);
        }

        // --- Baris 3: Cahaya (y: 34..47) ---
        uint8_t light_pct = (uint8_t)(((uint32_t)state->ldr_raw_adc * 100) / 4095);
        if (light_pct > 100) light_pct = 100;
        char val_ldr[16];
        snprintf(val_ldr, sizeof(val_ldr), "%2d %%", light_pct);
        uint8_t ldr_fill = (uint8_t)(((uint32_t)light_pct * 62) / 100);
        if (ldr_fill > 62) ldr_fill = 62;

        hud_ui_draw_icon_8x8(4, 34, icon_sensor_sun);
        hud_ui_draw_string(16, 35, "Cahaya", 1);
        hud_ui_draw_pixel(52, 38, 1);
        hud_ui_draw_pixel(54, 38, 1);
        hud_ui_draw_pixel(56, 38, 1);
        hud_ui_draw_string(58, 35, val_ldr, 1);
        hud_ui_draw_rect(15, 43, 66, 4, 1);
        if (ldr_fill > 0) {
            hud_ui_fill_rect(17, 44, ldr_fill, 2, 1);
        }
        return;
    }

    // =========================================================================
    // CASE 2: ONLY DHT ENABLED (LDR Disabled -> Cahaya dihilangkan dari screen)
    // 2 Baris Lapang & Simetris (y: 4..21 dan y: 26..43)
    // =========================================================================
    if (state->dht_enabled && !state->ldr_enabled) {
        // --- Baris 1: Suhu Udara ---
        char val_temp[16];
        uint8_t temp_fill = 0;
        if (state->dht_valid) {
            snprintf(val_temp, sizeof(val_temp), "%4.1f\x7F" "C", state->temperature_c);
            float t = state->temperature_c;
            if (t < 0.0f) t = 0.0f;
            if (t > 50.0f) t = 50.0f;
            temp_fill = (uint8_t)((t * 62.0f) / 50.0f);
            if (temp_fill > 62) temp_fill = 62;
        } else {
            snprintf(val_temp, sizeof(val_temp), "MEMBACA...");
        }

        hud_ui_draw_icon_8x10(4, 4, icon_sensor_thermo_hero);
        if (!state->dht_valid) {
            hud_ui_draw_string(16, 5, "Suhu", 1);
            hud_ui_draw_string(44, 5, val_temp, 1);
        } else {
            hud_ui_draw_string(16, 5, "Suhu", 1);
            hud_ui_draw_pixel(41, 8, 1);
            hud_ui_draw_pixel(43, 8, 1);
            hud_ui_draw_pixel(45, 8, 1);
            hud_ui_draw_string(47, 5, val_temp, 1);
        }
        hud_ui_draw_rect(15, 14, 66, 5, 1);
        if (temp_fill > 0) {
            hud_ui_fill_rect(17, 15, temp_fill, 3, 1);
        }

        // Divider tengah di y = 23
        for (int16_t x = 4; x < 80; x += 2) {
            hud_ui_draw_pixel(x, 23, 1);
        }

        // --- Baris 2: Kelembapan Udara ---
        char val_hum[16];
        uint8_t hum_fill = 0;
        if (state->dht_valid) {
            snprintf(val_hum, sizeof(val_hum), "%2.0f %%", state->humidity_pct);
            float h = state->humidity_pct;
            if (h < 0.0f) h = 0.0f;
            if (h > 100.0f) h = 100.0f;
            hum_fill = (uint8_t)((h * 62.0f) / 100.0f);
            if (hum_fill > 62) hum_fill = 62;
        } else {
            snprintf(val_hum, sizeof(val_hum), "-- %%");
        }

        hud_ui_draw_icon_8x8(4, 27, icon_sensor_drop);
        hud_ui_draw_string(16, 27, "Lembap", 1);
        hud_ui_draw_pixel(52, 30, 1);
        hud_ui_draw_pixel(54, 30, 1);
        hud_ui_draw_pixel(56, 30, 1);
        hud_ui_draw_string(58, 27, val_hum, 1);
        hud_ui_draw_rect(15, 36, 66, 5, 1);
        if (hum_fill > 0) {
            hud_ui_fill_rect(17, 37, hum_fill, 3, 1);
        }
        return;
    }

    // =========================================================================
    // CASE 3: ONLY LDR ENABLED (DHT Disabled -> Suhu & Lembap dihilangkan)
    // Tampilan Hero Sensor Cahaya Penuh
    // =========================================================================
    if (!state->dht_enabled && state->ldr_enabled) {
        uint8_t light_pct = (uint8_t)(((uint32_t)state->ldr_raw_adc * 100) / 4095);
        if (light_pct > 100) light_pct = 100;
        uint8_t ldr_fill = (uint8_t)(((uint32_t)light_pct * 68) / 100);
        if (ldr_fill > 68) ldr_fill = 68;

        // Header: Icon Sun & Label
        hud_ui_draw_icon_8x8(6, 6, icon_sensor_sun);
        hud_ui_draw_string(18, 6, "Sensor Cahaya", 1);
        for (int16_t x = 4; x < 80; x += 2) {
            hud_ui_draw_pixel(x, 16, 1);
        }

        // Nilai Cahaya Besar di Tengah (y: 20)
        char val_ldr[16];
        snprintf(val_ldr, sizeof(val_ldr), "%d %%", light_pct);
        int16_t vx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(val_ldr) * 6 - 1)) / 2;
        hud_ui_draw_string(vx, 20, val_ldr, 1);

        // Level Gauge Lebar 72px (x: 6..77, y: 29..34)
        hud_ui_draw_rect(6, 29, 72, 6, 1);
        if (ldr_fill > 0) {
            hud_ui_fill_rect(8, 31, ldr_fill, 2, 1);
        }

        // Status Ambien di Bawah (y: 38)
        const char *status_str = (light_pct >= 60) ? "TERANG (SIANG)" :
                                 ((light_pct >= 25) ? "REDUP (SENJA)" : "GELAP (MALAM)");
        int16_t sx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(status_str) * 6 - 1)) / 2;
        hud_ui_draw_string(sx, 38, status_str, 1);
        return;
    }
}

static void draw_media_player_page(const hud_state_t *state)
{
    // A. Inverted Banner across top (y: 0..8)
    hud_ui_fill_rect(0, 0, 84, 9, 1);
    hud_ui_draw_music_glyph(4, 1, 0);
    hud_ui_draw_string(14, 1, "MUSIK AKTIF", 0);
    hud_ui_draw_music_glyph(75, 1, 0);

    // B. Song Title with marquee (y: 12..18)
    char title_buf[16];
    get_marquee_ticker(title_buf, state->music_title, 13, 350);
    hud_ui_draw_string(3, 12, title_buf, 1);

    // C. Artist Name with marquee (y: 21..27)
    char artist_buf[16];
    const char *artist_src = (state->music_artist[0] != '\0') ? state->music_artist : "Audio HP";
    get_marquee_ticker(artist_buf, artist_src, 13, 400);
    hud_ui_draw_string(3, 21, artist_buf, 1);

    // D. Retro horizontal separator line at y: 30
    hud_ui_draw_line(2, 30, 81, 30, 1);

    // E. Bottom Area (y: 33..47 - 15px height): Large dancing equalizer + play indicator
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    for (int b = 0; b < 8; b++) {
        int h = 2 + (int)(sinf((now_ms / 130.0f) + b * 1.2f) * 5.0f + 5.5f);
        if (h < 2) h = 2;
        if (h > 13) h = 13;
        hud_ui_fill_rect(4 + b * 4, 47 - h, 2, h, 1);
    }

    // Playback status indicator
    hud_ui_draw_string(38, 36, "PUTAR >", 1);
}

typedef enum {
    STANDBY_PAGE_CLOCK = 0,
    STANDBY_PAGE_WEATHER,
    STANDBY_PAGE_SENSORS,
    STANDBY_PAGE_MEDIA,
} standby_page_t;

void hud_ui_draw_smartwatch_face(const hud_state_t *state)
{
    if (!state) return;

    // Riding lock: when moving (>= 3 km/h), lock to Clock / Speed display for rider safety
    if (state->speed_kmh >= 3.0f) {
        draw_clock_standby_page(state);
        return;
    }

    uint8_t cycle = state->ui_cycle_sec;
    if (cycle == 0) {
        draw_clock_standby_page(state);
        return;
    }

    // Build active pages dynamically
    standby_page_t active_pages[4];
    uint8_t num_pages = 0;

    // Page 0: Clock standby (always present)
    active_pages[num_pages++] = STANDBY_PAGE_CLOCK;

    // Page 1: Weather forecast (if weather data available)
    chronos_hourly_forecast_t fc;
    if (state->weather_valid || hud_state_get_forecast_1h(state, &fc)) {
        active_pages[num_pages++] = STANDBY_PAGE_WEATHER;
    }

    // Page 2: Sensors & Climate (if DHT or LDR enabled)
    if (state->dht_enabled || state->ldr_enabled) {
        active_pages[num_pages++] = STANDBY_PAGE_SENSORS;
    }

    // Page 3: Media player (only if media is active)
    if (is_media_active(state)) {
        active_pages[num_pages++] = STANDBY_PAGE_MEDIA;
    }

    if (num_pages <= 1) {
        draw_clock_standby_page(state);
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t cycle_ms = (uint32_t)cycle * 1000;
    uint8_t current_idx = (now_ms / cycle_ms) % num_pages;

    switch (active_pages[current_idx]) {
        case STANDBY_PAGE_CLOCK:
            draw_clock_standby_page(state);
            break;
        case STANDBY_PAGE_WEATHER:
            draw_weather_forecast_page(state);
            break;
        case STANDBY_PAGE_SENSORS:
            draw_sensor_climate_page(state);
            break;
        case STANDBY_PAGE_MEDIA:
            draw_media_player_page(state);
            break;
        default:
            draw_clock_standby_page(state);
            break;
    }
}

static void draw_fallback_turn_arrow(int16_t x0, int16_t y0, const char *dir_text)
{
    // Draw 24x24 border
    hud_ui_draw_rect(x0, y0, 24, 24, 1);

    bool is_left = (dir_text && (strstr(dir_text, "kiri") || strstr(dir_text, "Kiri") ||
                                 strstr(dir_text, "left") || strstr(dir_text, "Left")));
    bool is_right = (dir_text && (strstr(dir_text, "kanan") || strstr(dir_text, "Kanan") ||
                                  strstr(dir_text, "right") || strstr(dir_text, "Right")));
    bool is_uturn = (dir_text && (strstr(dir_text, "putar") || strstr(dir_text, "Putar") ||
                                  strstr(dir_text, "u-turn") || strstr(dir_text, "U-turn")));

    if (is_left) {
        // Left Turn Arrow (thick stem and arrowhead)
        for (int i = 0; i < 7; i++) {
            hud_ui_draw_line(x0 + 3 + i, y0 + 8 - i, x0 + 3 + i, y0 + 8 + i, 1);
        }
        hud_ui_fill_rect(x0 + 10, y0 + 7, 9, 3, 1);
        hud_ui_fill_rect(x0 + 16, y0 + 10, 3, 12, 1);
    } else if (is_right) {
        // Right Turn Arrow
        for (int i = 0; i < 7; i++) {
            hud_ui_draw_line(x0 + 20 - i, y0 + 8 - i, x0 + 20 - i, y0 + 8 + i, 1);
        }
        hud_ui_fill_rect(x0 + 5, y0 + 7, 9, 3, 1);
        hud_ui_fill_rect(x0 + 5, y0 + 10, 3, 12, 1);
    } else if (is_uturn) {
        // U-Turn Arrow
        for (int i = 0; i < 6; i++) {
            hud_ui_draw_line(x0 + 6 - i, y0 + 16 - i, x0 + 6 + i, y0 + 16 - i, 1);
        }
        hud_ui_fill_rect(x0 + 5, y0 + 10, 3, 6, 1);
        hud_ui_fill_rect(x0 + 5, y0 + 7, 13, 3, 1);
        hud_ui_fill_rect(x0 + 15, y0 + 10, 3, 12, 1);
    } else {
        // Straight Arrow (pointing up)
        for (int i = 0; i < 8; i++) {
            hud_ui_draw_line(x0 + 11 - i, y0 + 2 + i, x0 + 12 + i, y0 + 2 + i, 1);
        }
        hud_ui_fill_rect(x0 + 10, y0 + 10, 3, 12, 1);
    }
}

static bool extract_distance_from_string(const char *src, char *out_dist, size_t max_len)
{
    if (!src || src[0] == '\0' || !out_dist || max_len == 0) return false;

    const char *p = src;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            const char *num_start = p;
            while (*p && ((*p >= '0' && *p <= '9') || *p == '.' || *p == ',' || *p == ' ')) {
                p++;
            }
            if (*p != '\0') {
                const char *u_end = NULL;
                // km or KM
                if ((p[0] == 'k' || p[0] == 'K') && (p[1] == 'm' || p[1] == 'M')) {
                    u_end = p + 2;
                }
                // mi or MI (miles)
                else if ((p[0] == 'm' || p[0] == 'M') && (p[1] == 'i' || p[1] == 'I')) {
                    u_end = p + 2;
                }
                // ft or FT (feet)
                else if ((p[0] == 'f' || p[0] == 'F') && (p[1] == 't' || p[1] == 'T')) {
                    u_end = p + 2;
                }
                // yd or YD (yards)
                else if ((p[0] == 'y' || p[0] == 'Y') && (p[1] == 'd' || p[1] == 'D')) {
                    u_end = p + 2;
                }
                // m or M (meter) - check it's not "min", "menit", "mnt"
                else if (p[0] == 'm' || p[0] == 'M') {
                    char next = p[1];
                    if (next == ' ' || next == '\0' || next == ',' || next == '.' ||
                        next == '-' || next == '/' || next == ')' || (next >= '0' && next <= '9')) {
                        u_end = p + 1;
                    }
                }

                if (u_end != NULL) {
                    const char *num_end = p;
                    while (num_end > num_start && *(num_end - 1) == ' ') {
                        num_end--;
                    }
                    size_t n_len = (size_t)(num_end - num_start);
                    size_t u_len = (size_t)(u_end - p);

                    if (n_len > 0 && n_len + u_len + 2 < max_len) {
                        snprintf(out_dist, max_len, "%.*s %.*s", (int)n_len, num_start, (int)u_len, p);
                        return true;
                    }
                }
            }
        } else {
            p++;
        }
    }
    return false;
}

static void hud_ui_extract_turn_distance(const hud_state_t *state, char *out_dist, size_t max_len)
{
    if (!out_dist || max_len == 0) return;
    out_dist[0] = '\0';
    if (!state) return;

    // 1. Primary: Extract turn distance from nav_title (Chronos standard for distance to next point/maneuver)
    if (extract_distance_from_string(state->nav_title, out_dist, max_len)) {
        return;
    }

    // 2. Secondary: Extract turn distance from nav_directions (if instruction embeds maneuver distance)
    if (extract_distance_from_string(state->nav_directions, out_dist, max_len)) {
        return;
    }

    // 3. Tertiary: Check nav_distance (e.g. stretch distance or mock tests)
    if (extract_distance_from_string(state->nav_distance, out_dist, max_len)) {
        return;
    }
    if (state->nav_distance[0] != '\0') {
        strncpy(out_dist, state->nav_distance, max_len - 1);
        out_dist[max_len - 1] = '\0';
        return;
    }

    // 4. Default: If navigation is active but no advance distance is found, show "0 m" (NEVER "DEKAT")
    strncpy(out_dist, "0 m", max_len - 1);
    out_dist[max_len - 1] = '\0';
}

void hud_ui_draw_multitask_nav(const hud_state_t *state)
{
    if (!state) return;

    // Left Turn Icon (x: 2..25, y: 5..28) - vertically centered in upper area
    if (state->nav_icon_valid) {
        // Real Google Maps icon dynamically sent from Chronos app
        hud_ui_blit_chronos_icon_24x24(state->nav_icon_bmp, 2, 5);
    } else {
        // Smart procedural directional arrow while waiting for Chronos bitmap
        const char *dir_src = (state->nav_directions[0] != '\0') ? state->nav_directions :
                              ((state->nav_title[0] != '\0') ? state->nav_title : "");
        draw_fallback_turn_arrow(2, 5, dir_src);
    }

    // Extract Turn Distance reliably
    char dist_str[32];
    hud_ui_extract_turn_distance(state, dist_str, sizeof(dist_str));

    // Direction string for Row 2: prioritize directions, fallback to title (ensure no duplicate of dist_str)
    const char *dir_src = (state->nav_directions[0] != '\0') ? state->nav_directions :
                          ((state->nav_title[0] != '\0') ? state->nav_title : "Lurus");
    if (strcasecmp(dir_src, dist_str) == 0) {
        dir_src = "Lurus";
    }

    // Speed display flag: show speed if hardware VSS is enabled, OR if Maps/GPS speed is valid
    bool show_speed = state->vss_enabled || state->ble_gps_valid;

    // Right Telemetry (x: 28..83)
    if (show_speed) {
        // Row 1: Distance to Turn (y: 2..10) in an eye-catching high-contrast badge
        int16_t dw = (int16_t)(strlen(dist_str) * 6) + 3;
        if (dw > 55) dw = 55;
        hud_ui_fill_rect(28, 2, dw, 9, 1);
        hud_ui_draw_string(30, 3, dist_str, 0);

        // Row 2: Direction / Street (y: 12..19)
        char dir_buf[16];
        get_marquee_ticker(dir_buf, dir_src, 9, 500);
        hud_ui_draw_string(28, 12, dir_buf, 1);

        // Row 3: Current speed (y: 22..29)
        char spd_buf[16];
        snprintf(spd_buf, sizeof(spd_buf), "%2.0f km/h", state->speed_kmh);
        hud_ui_draw_string(28, 22, spd_buf, 1);
    } else {
        // Motorcycle sensor disabled and no speed from Maps: hide speed row completely!
        // Generous vertical space for Turn Distance & Next Road Name:
        int16_t dw = (int16_t)(strlen(dist_str) * 6) + 5;
        if (dw > 54) dw = 54;
        hud_ui_fill_rect(28, 4, dw, 11, 1);
        hud_ui_draw_string(30, 6, dist_str, 0);

        // Row 2: Direction / Next Street Name (y: 18..25)
        char dir_buf[16];
        get_marquee_ticker(dir_buf, dir_src, 9, 500);
        hud_ui_draw_string(28, 18, dir_buf, 1);
    }

    // Divider line at y: 33
    hud_ui_draw_line(0, 33, 83, 33, 1);

    // Bottom Area (y: 35..43): Clock on left (x=0..28),
    // Right area cleanly alternates between Total Trip Distance (e.g. "4.3 km") and Arrival Time ("ETA 18:19")!
    int hour = 12, min = 0;
    if (state->time_synced) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        hour = timeinfo.tm_hour;
        min = timeinfo.tm_min;
    }

    char c_sep = is_clock_colon_visible(state->time_synced) ? ':' : ' ';
    char clock_str[8];
    snprintf(clock_str, sizeof(clock_str), "%02d%c%02d", hour, c_sep, min);
    hud_ui_draw_string(0, 35, clock_str, 1);

    // Dynamic right info: toggle smoothly every 3s between Total Destination Distance and ETA
    uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    char info_str[48];
    bool show_total_dist = (state->nav_distance[0] != '\0' && (now_sec / 3) % 2 == 1);

    if (show_total_dist) {
        snprintf(info_str, sizeof(info_str), "%s", state->nav_distance);
    } else if (state->nav_eta[0] != '\0') {
        if (strncasecmp(state->nav_eta, "ETA", 3) == 0) {
            snprintf(info_str, sizeof(info_str), "%s", state->nav_eta);
        } else {
            snprintf(info_str, sizeof(info_str), "ETA %s", state->nav_eta);
        }
    } else if (state->nav_distance[0] != '\0') {
        snprintf(info_str, sizeof(info_str), "%s", state->nav_distance);
    } else if (state->nav_duration[0] != '\0') {
        snprintf(info_str, sizeof(info_str), "%s", state->nav_duration);
    } else {
        snprintf(info_str, sizeof(info_str), "GPS ON");
    }

    int16_t info_w = (int16_t)(strlen(info_str) * 6 - 1);
    int16_t info_x = 83 - info_w;
    if (info_x < 30) info_x = 30; // Prevent overlapping clock (ends at x=28)

    // Crisp vertical divider line between Clock and Trip Info if space allows
    if (info_x >= 32) {
        hud_ui_draw_line(30, 35, 30, 43, 1);
    }

    hud_ui_draw_string(info_x, 35, info_str, 1);

    // Dynamic Turn Distance Countdown Bar (y: 46..47)
    // Progressively counts down towards 0 as rider approaches the turn point (within 500m)
    if (dist_str[0] != '\0') {
        float dval = strtof(dist_str, NULL);
        bool is_km = (strstr(dist_str, "km") != NULL || strstr(dist_str, "KM") != NULL);
        float dist_m = is_km ? (dval * 1000.0f) : dval;

        if (dist_m <= 500.0f) {
            int16_t bar_w = (int16_t)((dist_m * 84.0f) / 500.0f);
            if (bar_w < 2 && dist_m > 3.0f) bar_w = 2;
            if (bar_w > 84) bar_w = 84;

            // Dotted baseline track across bottom:
            for (int16_t bx = 0; bx < 84; bx += 2) {
                hud_ui_draw_pixel(bx, 47, 1);
            }
            // Solid remaining distance progress bar:
            if (bar_w > 0) {
                hud_ui_fill_rect(0, 46, bar_w, 2, 1);
            }
        }
    }
}

static void hud_ui_draw_retro_ringing_phone(int16_t cx, int16_t cy, uint32_t now_ms)
{
    // Retro phone handset bitmap (14x7 px):
    static const uint16_t s_handset[7] = {
        0b1100000000001100, // XX..........XX
        0b1110000000011100, // XXX........XXX
        0b1111000000111100, // XXXX......XXXX
        0b0111111111111000, // .XXXXXXXXXXXX.
        0b0011111111110000, // ..XXXXXXXXXX..
        0b0001111111100000, // ...XXXXXXXX...
        0b0000011110000000  // .....XXXX.....
    };

    uint8_t step = (now_ms / 180) % 4;
    int16_t hx = cx - 7;
    int16_t hy = cy - 3;
    if (step == 0) hy -= 1;
    else if (step == 2) hy += 1;

    // Draw handset
    for (int r = 0; r < 7; r++) {
        uint16_t row = s_handset[r];
        for (int c = 0; c < 14; c++) {
            if (row & (1 << (15 - c))) {
                hud_ui_draw_pixel(hx + c, hy + r, 1);
            }
        }
    }

    // Vibrating sound wave arcs on left and right:
    if (step % 2 == 0) {
        // Inner left arc:
        hud_ui_draw_line(cx - 10, cy - 3, cx - 12, cy, 1);
        hud_ui_draw_line(cx - 12, cy, cx - 10, cy + 3, 1);
        // Inner right arc:
        hud_ui_draw_line(cx + 10, cy - 3, cx + 12, cy, 1);
        hud_ui_draw_line(cx + 12, cy, cx + 10, cy + 3, 1);
    }
    if (step == 1 || step == 2) {
        // Outer left arc:
        hud_ui_draw_line(cx - 15, cy - 4, cx - 18, cy, 1);
        hud_ui_draw_line(cx - 18, cy, cx - 15, cy + 4, 1);
        // Outer right arc:
        hud_ui_draw_line(cx + 15, cy - 4, cx + 18, cy, 1);
        hud_ui_draw_line(cx + 18, cy, cx + 15, cy + 4, 1);
    }
}

void hud_ui_draw_incoming_call_overlay(const hud_state_t *state)
{
    if (!state) return;

    // 1. Inverted Banner across top (y: 0..8) - Centered & NO CUTOFF
    hud_ui_fill_rect(0, 0, 84, 9, 1);
    hud_ui_draw_string(15, 1, "PANGGILAN", 0);

    // 2. Caller Name / Number (y: 12..19)
    const char *caller_src = (state->notif_message[0] != '\0') ? state->notif_message :
                             ((state->notif_title[0] != '\0') ? state->notif_title : "Nomor Pribadi");

    size_t c_len = strlen(caller_src);
    if (c_len <= 13) {
        int16_t cx = (HUD_SCREEN_WIDTH - (int16_t)(c_len * 6 - 1)) / 2;
        if (cx < 2) cx = 2;
        hud_ui_draw_string(cx, 12, caller_src, 1);
    } else {
        // Long name: smooth marquee so NO text is ever cut off!
        char t_crop[16];
        get_marquee_ticker(t_crop, caller_src, 13, 350);
        hud_ui_draw_string(2, 12, t_crop, 1);
    }

    // 3. Subtitle / Status (y: 22..29)
    if (state->nav_active) {
        char nav_rem[48];
        const char *dist = (state->nav_distance[0] != '\0') ? state->nav_distance : "Rute";
        snprintf(nav_rem, sizeof(nav_rem), "[Nav: %s]", dist);
        int16_t nx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(nav_rem) * 6 - 1)) / 2;
        if (nx < 2) nx = 2;
        hud_ui_draw_string(nx, 22, nav_rem, 1);
    } else if (state->notif_title[0] != '\0' && strcasecmp(state->notif_title, caller_src) != 0 &&
               strcasecmp(state->notif_title, "call") != 0 && strcasecmp(state->notif_title, "panggilan") != 0) {
        char sub[16];
        truncate_string_ellipsis(sub, state->notif_title, 13);
        int16_t sx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(sub) * 6 - 1)) / 2;
        if (sx < 2) sx = 2;
        hud_ui_draw_string(sx, 22, sub, 1);
    } else {
        hud_ui_draw_string(12, 22, "Telepon HP", 1);
    }

    // 4. Retro Nokia Ringing Telephone Animation (y: 33..47)
    hud_ui_draw_line(0, 32, 83, 32, 1);
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    hud_ui_draw_retro_ringing_phone(42, 40, now_ms);
}

static size_t extract_notif_line(char *dest, const char *src, size_t offset, size_t max_len, bool add_ellipsis_if_more)
{
    if (!dest || max_len == 0) return offset;
    dest[0] = '\0';
    if (!src) return offset;

    size_t slen = strlen(src);
    while (offset < slen && (src[offset] == ' ' || src[offset] == '\r' || src[offset] == '\n')) {
        offset++;
    }
    if (offset >= slen) {
        return offset;
    }

    // Check if there is an explicit newline within max_len
    size_t rem = slen - offset;
    size_t limit = (rem < max_len) ? rem : max_len;
    for (size_t i = 0; i < limit; i++) {
        if (src[offset + i] == '\n' || src[offset + i] == '\r') {
            strncpy(dest, src + offset, i);
            dest[i] = '\0';
            return offset + i + 1;
        }
    }

    if (rem <= max_len) {
        strncpy(dest, src + offset, rem);
        dest[rem] = '\0';
        return offset + rem;
    }

    size_t break_pt = max_len;
    for (size_t i = max_len; i >= 3; i--) {
        if (src[offset + i] == ' ') {
            break_pt = i;
            break;
        }
    }

    if (add_ellipsis_if_more && (offset + break_pt < slen)) {
        if (break_pt > 2) {
            strncpy(dest, src + offset, break_pt - 2);
            dest[break_pt - 2] = '.';
            dest[break_pt - 1] = '.';
            dest[break_pt] = '\0';
        } else {
            strncpy(dest, src + offset, break_pt);
            dest[break_pt] = '\0';
        }
    } else {
        strncpy(dest, src + offset, break_pt);
        dest[break_pt] = '\0';
    }

    return offset + break_pt;
}

#define NOTIF_MAX_PAGES 5
#define NOTIF_LINES_PER_PAGE 4
#define NOTIF_MAX_TOTAL_LINES (NOTIF_MAX_PAGES * NOTIF_LINES_PER_PAGE) // 20 lines
#define NOTIF_CHARS_PER_LINE 13

static const char *resolve_app_name(const hud_state_t *state, char *custom_app_buf, size_t buf_size)
{
    if (!state) return "NOTIFIKASI";

    // 1. Check title and message content for navigation and explicit app keywords
    if (strcasestr(state->notif_title, "maps") != NULL ||
        strcasestr(state->notif_message, "maps") != NULL ||
        strcasestr(state->notif_title, "gmaps") != NULL ||
        strcasestr(state->notif_message, "gmaps") != NULL ||
        strcasestr(state->notif_title, "petunjuk arah") != NULL ||
        strcasestr(state->notif_message, "petunjuk arah") != NULL ||
        strcasestr(state->notif_title, "navigasi") != NULL ||
        strcasestr(state->notif_message, "navigasi") != NULL) {
        return "GMAPS";
    }
    if (strcasestr(state->notif_title, "waze") != NULL ||
        strcasestr(state->notif_message, "waze") != NULL) {
        return "WAZE";
    }
    if (strcasestr(state->notif_title, "discord") != NULL ||
        strcasestr(state->notif_message, "discord") != NULL) {
        return "DISCORD";
    }
    if (strcasestr(state->notif_title, "whatsapp") != NULL ||
        strcasestr(state->notif_message, "whatsapp") != NULL) {
        return "WHATSAPP";
    }
    if (strcasestr(state->notif_title, "telegram") != NULL ||
        strcasestr(state->notif_message, "telegram") != NULL) {
        return "TELEGRAM";
    }
    if (strcasestr(state->notif_title, "instagram") != NULL ||
        strcasestr(state->notif_message, "instagram") != NULL) {
        return "INSTAGRAM";
    }
    if (strcasestr(state->notif_title, "gmail") != NULL ||
        strcasestr(state->notif_message, "gmail") != NULL) {
        return "GMAIL";
    }
    if (strcasestr(state->notif_title, "twitter") != NULL ||
        strcasestr(state->notif_message, "twitter") != NULL) {
        return "TWITTER";
    }
    if (strcasestr(state->notif_title, "messenger") != NULL) {
        return "MESSENGER";
    }
    if (strcasestr(state->notif_title, "facebook") != NULL ||
        strcasestr(state->notif_message, "facebook") != NULL) {
        return "FACEBOOK";
    }
    if (strcasestr(state->notif_title, "tiktok") != NULL ||
        strcasestr(state->notif_message, "tiktok") != NULL) {
        return "TIKTOK";
    }
    if (strcasestr(state->notif_title, "youtube") != NULL ||
        strcasestr(state->notif_message, "youtube") != NULL) {
        return "YOUTUBE";
    }
    if (strcasestr(state->notif_title, "shopee") != NULL ||
        strcasestr(state->notif_message, "shopee") != NULL) {
        return "SHOPEE";
    }
    if (strcasestr(state->notif_title, "tokopedia") != NULL ||
        strcasestr(state->notif_message, "tokopedia") != NULL) {
        return "TOKOPEDIA";
    }
    if (strcasestr(state->notif_title, "gojek") != NULL ||
        strcasestr(state->notif_message, "gojek") != NULL) {
        return "GOJEK";
    }
    if (strcasestr(state->notif_title, "grab") != NULL ||
        strcasestr(state->notif_message, "grab") != NULL) {
        return "GRAB";
    }

    // 2. Protocol icon ID mappings
    switch (state->notif_app_id) {
        case CHRONOS_APP_MAPS: return "GMAPS";
        case CHRONOS_APP_WAZE: return "WAZE";
        case CHRONOS_APP_DISCORD: return "DISCORD";
        case CHRONOS_APP_WHATSAPP: return "WHATSAPP";
        case CHRONOS_APP_WA_BIZ: return "WA BISNIS";
        case CHRONOS_APP_TELEGRAM: return "TELEGRAM";
        case CHRONOS_APP_INSTAGRAM: return "INSTAGRAM";
        case CHRONOS_APP_GMAIL: return "GMAIL";
        case CHRONOS_APP_EMAIL: return "EMAIL";
        case CHRONOS_APP_TWITTER:
        case CHRONOS_APP_TWITTER_OLD: return "TWITTER";
        case CHRONOS_APP_FACEBOOK: return "FACEBOOK";
        case CHRONOS_APP_MESSENGER: return "MESSENGER";
        case CHRONOS_APP_LINE: return "LINE";
        case CHRONOS_APP_SYSTEM: return "CHRONOS";
        case CHRONOS_APP_MESSAGE:
            if (strcasestr(state->notif_title, "sms") != NULL ||
                strcasestr(state->notif_message, "sms") != NULL ||
                (state->notif_title[0] == '+' || (state->notif_title[0] >= '0' && state->notif_title[0] <= '9'))) {
                return "SMS";
            }
            return "NOTIFIKASI";
        default:
            break;
    }

    // 3. Fallback to valid title if available
    if (state->notif_title[0] != '\0' &&
        strcasecmp(state->notif_title, "notification") != 0 &&
        strcasecmp(state->notif_title, "notifikasi") != 0 &&
        strcasecmp(state->notif_title, "pesan") != 0 &&
        strcasecmp(state->notif_title, "pesan baru") != 0) {
        if (custom_app_buf && buf_size > 0) {
            truncate_string_ellipsis(custom_app_buf, state->notif_title, (buf_size < 13) ? (uint8_t)buf_size : 12);
            return custom_app_buf;
        }
        return state->notif_title;
    }

    return "NOTIFIKASI";
}

static void hud_ui_prepare_notif_body(const hud_state_t *state, const char *app_name, char *out_buf, size_t buf_size)
{
    if (!state || !out_buf || buf_size == 0) return;
    out_buf[0] = '\0';

    const char *title = state->notif_title;
    const char *msg = state->notif_message;

    // Strip redundant leading app name from msg
    if (app_name != NULL && app_name[0] != '\0') {
        size_t app_len = strlen(app_name);
        if (strncasecmp(msg, app_name, app_len) == 0) {
            const char *p = msg + app_len;
            while (*p == ' ' || *p == ':') p++;
            msg = p;
        }
        if (strncasecmp(title, app_name, app_len) == 0) {
            const char *p = title + app_len;
            while (*p == ' ' || *p == ':') p++;
            title = p;
        }
    }

    // Strip bracketed prefixes from msg e.g. "[Discord] "
    if (msg[0] == '[') {
        const char *rb = strchr(msg, ']');
        if (rb && (size_t)(rb - msg) < 15) {
            msg = rb + 1;
            while (*msg == ' ' || *msg == ':') msg++;
        }
    }

    bool has_valid_title = (title[0] != '\0' &&
                           strcasecmp(title, "notification") != 0 &&
                           strcasecmp(title, "notifikasi") != 0 &&
                           strcasecmp(title, "pesan") != 0 &&
                           strcasecmp(title, "sms") != 0 &&
                           (app_name == NULL || strcasecmp(title, app_name) != 0) &&
                           strstr(msg, title) == NULL);

    if (has_valid_title) {
        snprintf(out_buf, buf_size, "%s: %s", title, (msg[0] != '\0') ? msg : "...");
    } else {
        snprintf(out_buf, buf_size, "%s", (msg[0] != '\0') ? msg : "...");
    }
}

static int hud_ui_split_notif_lines(const char *msg, char lines[NOTIF_MAX_TOTAL_LINES][16])
{
    if (!msg || msg[0] == '\0') return 0;

    int line_count = 0;
    size_t off = 0;
    size_t slen = strlen(msg);

    while (off < slen && line_count < NOTIF_MAX_TOTAL_LINES) {
        bool is_last_allowed = (line_count == NOTIF_MAX_TOTAL_LINES - 1);
        size_t next_off = extract_notif_line(lines[line_count], msg, off, NOTIF_CHARS_PER_LINE, is_last_allowed);
        if (next_off <= off) break;
        off = next_off;
        line_count++;
    }
    return line_count;
}

static void hud_ui_get_notif_timing(const hud_state_t *state, uint32_t *out_total_duration_ms, int *out_total_pages, int *out_current_page)
{
    if (!state) {
        if (out_total_duration_ms) *out_total_duration_ms = 7000;
        if (out_total_pages) *out_total_pages = 1;
        if (out_current_page) *out_current_page = 0;
        return;
    }

    char custom_app_buf[16];
    const char *app_name = resolve_app_name(state, custom_app_buf, sizeof(custom_app_buf));
    char full_msg[320];
    hud_ui_prepare_notif_body(state, app_name, full_msg, sizeof(full_msg));

    char dummy_lines[NOTIF_MAX_TOTAL_LINES][16];
    int line_count = hud_ui_split_notif_lines(full_msg, dummy_lines);
    int total_pages = (line_count + NOTIF_LINES_PER_PAGE - 1) / NOTIF_LINES_PER_PAGE;
    if (total_pages < 1) total_pages = 1;
    if (total_pages > NOTIF_MAX_PAGES) total_pages = NOTIF_MAX_PAGES;

    uint32_t base_sec = (state->notif_timeout_sec >= 3) ? state->notif_timeout_sec : 7;
    uint32_t total_duration_ms = base_sec * 1000;
    if (total_pages > 1) {
        uint32_t min_multi_ms = (uint32_t)(total_pages * 2500);
        if (total_duration_ms < min_multi_ms) {
            total_duration_ms = min_multi_ms;
        }
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t elapsed_ms = 0;
    if (state->notif_timestamp > 0 && now_ms >= state->notif_timestamp) {
        elapsed_ms = now_ms - state->notif_timestamp;
    }

    uint32_t page_duration_ms = total_duration_ms / total_pages;
    int current_page = (page_duration_ms > 0) ? (int)(elapsed_ms / page_duration_ms) : 0;
    if (current_page >= total_pages) current_page = total_pages - 1;

    if (out_total_duration_ms) *out_total_duration_ms = total_duration_ms;
    if (out_total_pages) *out_total_pages = total_pages;
    if (out_current_page) *out_current_page = current_page;
}

void hud_ui_draw_notification_overlay(const hud_state_t *state)
{
    if (!state) return;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t elapsed_ms = 0;
    if (state->notif_timestamp > 0 && now_ms >= state->notif_timestamp) {
        elapsed_ms = now_ms - state->notif_timestamp;
    }

    uint32_t total_duration_ms = 7000;
    int total_pages = 1;
    int current_page = 0;
    hud_ui_get_notif_timing(state, &total_duration_ms, &total_pages, &current_page);

    char custom_app_buf[16];
    const char *app_name = resolve_app_name(state, custom_app_buf, sizeof(custom_app_buf));

    // 1. Inverted Header across top (y: 0..8): App Name with Auto-Pagination (e.g. [ DISCORD (1/2) ])
    hud_ui_fill_rect(0, 0, 84, 9, 1);
    char hdr[48];
    if (total_pages > 1) {
        snprintf(hdr, sizeof(hdr), "%s (%d/%d)", app_name, current_page + 1, total_pages);
        if (strlen(hdr) > 13) {
            snprintf(hdr, sizeof(hdr), "%s %d/%d", app_name, current_page + 1, total_pages);
        }
        if (strlen(hdr) > 13) {
            snprintf(hdr, sizeof(hdr), "%.*s %d/%d", 13 - 4, app_name, current_page + 1, total_pages);
        }
    } else {
        snprintf(hdr, sizeof(hdr), "%s", app_name);
        if (strlen(hdr) > 13) {
            truncate_string_ellipsis(hdr, app_name, 13);
        }
    }

    int16_t hx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(hdr) * 6 - 1)) / 2;
    if (hx < 1) hx = 1;
    hud_ui_draw_string(hx, 1, hdr, 0); // Inverted white text on black bar

    // 2. Message Body (y: 11..44) - 4 full clean lines for active page!
    char full_msg[320];
    hud_ui_prepare_notif_body(state, app_name, full_msg, sizeof(full_msg));

    char lines[NOTIF_MAX_TOTAL_LINES][16];
    int line_count = hud_ui_split_notif_lines(full_msg, lines);

    int max_msg_lines = (state->nav_active && state->nav_distance[0] != '\0') ? 3 : NOTIF_LINES_PER_PAGE;

    for (int i = 0; i < max_msg_lines; i++) {
        int idx = current_page * NOTIF_LINES_PER_PAGE + i;
        if (idx < line_count && lines[idx][0] != '\0') {
            hud_ui_draw_string(2, 11 + i * 9, lines[idx], 1);
        }
    }

    // If navigating, render compact Mini-Nav reminder on line 4 (y: 38)
    if (state->nav_active && state->nav_distance[0] != '\0') {
        char nav_rem[128];
        const char *turn_dir = (state->nav_directions[0] != '\0') ? state->nav_directions :
                               ((state->nav_title[0] != '\0') ? state->nav_title : "Rute");
        snprintf(nav_rem, sizeof(nav_rem), "Nav:%s %s", state->nav_distance, turn_dir);
        char tick[16];
        get_marquee_ticker(tick, nav_rem, 13, 350);
        hud_ui_draw_string(2, 38, tick, 1);
    }

    // 3. Silent Progress Line (y: 47) showing overall countdown
    if (elapsed_ms < total_duration_ms && total_duration_ms > 0) {
        int16_t bar_w = (int16_t)(((total_duration_ms - elapsed_ms) * 84) / total_duration_ms);
        if (bar_w > 0) {
            hud_ui_draw_line(0, 47, bar_w - 1, 47, 1);
        }
    }
}

void hud_ui_draw_cruising_hud(const hud_state_t *state)
{
    hud_ui_draw_smartwatch_face(state);
}

static void draw_nav_cruising_page(const hud_state_t *state)
{
    int hour = 12, min = 0;
    if (state->time_synced) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        hour = timeinfo.tm_hour;
        min = timeinfo.tm_min;
    }

    // A. Big 7-Segment Clock (y: 2..23, 22px high)
    // Digits placed at x: 1..62 to leave right margin (x: 64..83) for Phone Battery telemetry
    hud_ui_draw_large_digit(1, 2, (hour / 10) + '0', 1);
    hud_ui_draw_large_digit(16, 2, (hour % 10) + '0', 1);

    // Blinking Colon dots: two 2x2 squares at (31, 7) and (31, 14) (1 Hz pulse)
    if (is_clock_colon_visible(state->time_synced)) {
        hud_ui_fill_rect(31, 7, 2, 2, 1);
        hud_ui_fill_rect(31, 14, 2, 2, 1);
    }

    hud_ui_draw_large_digit(34, 2, (min / 10) + '0', 1);
    hud_ui_draw_large_digit(49, 2, (min % 10) + '0', 1);

    // Android BLE Connection Indicator (Bluetooth Icon at x: 64, y: 3)
    if (state->ble_connected) {
        hud_ui_draw_ble_glyph(64, 3, 1);
    }

    // Phone Battery Telemetry (Icon & Percentage) on right side of Clock
    uint8_t bat_pct = state->phone_battery_valid ? state->phone_battery_pct : 0;
    bool charging = state->phone_battery_valid ? state->phone_charging : false;
    hud_ui_draw_nokia_battery(71, 3, bat_pct, charging);

    char bat_str[8];
    if (state->phone_battery_valid) {
        if (bat_pct >= 100) {
            snprintf(bat_str, sizeof(bat_str), "100");
        } else {
            snprintf(bat_str, sizeof(bat_str), "%d%%", bat_pct);
        }
    } else if (state->ble_connected) {
        snprintf(bat_str, sizeof(bat_str), "--%%");
    } else {
        snprintf(bat_str, sizeof(bat_str), "OFF");
    }
    int16_t bw = (int16_t)(strlen(bat_str) * 6 - 1);
    int16_t bx = 83 - bw;
    if (bx < 64) bx = 64;
    hud_ui_draw_string(bx, 13, bat_str, 1);

    // Divider line at y: 26
    hud_ui_draw_line(0, 26, 83, 26, 1);

    // B. Middle Row (y: 28..35): Turn Distance & ETA or Speed (NO TRUNCATION!)
    char dist_str[32];
    hud_ui_extract_turn_distance(state, dist_str, sizeof(dist_str));
    bool show_speed = state->vss_enabled || state->ble_gps_valid;

    uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    char info_str[48];
    bool show_total_dist = (state->nav_distance[0] != '\0' && (now_sec / 3) % 2 == 1);

    if (show_total_dist) {
        snprintf(info_str, sizeof(info_str), "%s", state->nav_distance);
    } else if (state->nav_eta[0] != '\0') {
        if (strncasecmp(state->nav_eta, "ETA", 3) == 0) {
            snprintf(info_str, sizeof(info_str), "%s", state->nav_eta);
        } else {
            snprintf(info_str, sizeof(info_str), "ETA %s", state->nav_eta);
        }
    } else if (state->nav_distance[0] != '\0') {
        snprintf(info_str, sizeof(info_str), "%s", state->nav_distance);
    } else if (state->nav_duration[0] != '\0') {
        snprintf(info_str, sizeof(info_str), "%s", state->nav_duration);
    } else {
        snprintf(info_str, sizeof(info_str), "GPS ON");
    }

    if (show_speed) {
        char spd_str[16];
        snprintf(spd_str, sizeof(spd_str), "%2.0f km/h", state->speed_kmh);
        hud_ui_draw_string(2, 28, spd_str, 1);

        int16_t ew = (int16_t)(strlen(info_str) * 6 - 1);
        int16_t ex = 83 - ew;
        if (ex < 38) ex = 38;
        hud_ui_draw_string(ex, 28, info_str, 1);
    } else {
        // Motorcycle speed disabled: Left shows Turn Distance in inverted badge, Right shows full ETA / Total Dist!
        int16_t dw = (int16_t)(strlen(dist_str) * 6) + 3;
        if (dw > 42) dw = 42;
        hud_ui_fill_rect(2, 27, dw, 9, 1);
        hud_ui_draw_string(4, 28, dist_str, 0);

        int16_t ew = (int16_t)(strlen(info_str) * 6 - 1);
        int16_t ex = 83 - ew;
        if (ex < dw + 3) ex = dw + 3;
        hud_ui_draw_string(ex, 28, info_str, 1);
    }

    // C. Bottom Row: Upcoming Maneuver Prompt Ticker (y: 38..44)
    char nav_prompt[128];
    const char *turn_dir = (state->nav_directions[0] != '\0') ? state->nav_directions :
                           ((state->nav_title[0] != '\0') ? state->nav_title : "Lurus");
    if (dist_str[0] != '\0') {
        snprintf(nav_prompt, sizeof(nav_prompt), "Nav: %s %s", dist_str, turn_dir);
    } else {
        snprintf(nav_prompt, sizeof(nav_prompt), "Nav: %s", turn_dir);
    }
    char tick[16];
    get_marquee_ticker(tick, nav_prompt, 13, 350);
    hud_ui_draw_string(2, 38, tick, 1);
}

void hud_ui_draw_nav_hud(const hud_state_t *state)
{
    if (!state) return;

    // NOTE: Navbar (status bar) is omitted on navigation screen per user request,
    // maximizing glanceable screen real estate for turn icons, distances, road names, and ETA!

    // 1. Safety Override: If turn distance is <= 100m, lock to Main Turn screen
    char dist_str[32];
    hud_ui_extract_turn_distance(state, dist_str, sizeof(dist_str));
    if (dist_str[0] != '\0') {
        float dist_val = strtof(dist_str, NULL);
        bool is_km = (strstr(dist_str, "km") != NULL || strstr(dist_str, "KM") != NULL);
        if ((is_km && dist_val <= 0.10f) || (!is_km && dist_val <= 100.0f)) {
            hud_ui_draw_multitask_nav(state);
            return;
        }
    }

    // 2. Navigation Cycle parameter check
    uint8_t cycle = state->nav_cycle_sec;
    if (cycle == 0) {
        hud_ui_draw_multitask_nav(state);
        return;
    }

    // 3. Dynamic split cycle: Main Turn Screen takes majority of duration, Cruising Screen takes minority
    uint32_t main_sec = cycle;
    uint32_t sub_sec = (main_sec >= 20) ? 5 : ((main_sec >= 15) ? 4 : 3);
    uint32_t total_sec = main_sec + sub_sec;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t sec_in_cycle = (now_ms / 1000) % total_sec;

    if (sec_in_cycle < main_sec) {
        hud_ui_draw_multitask_nav(state);
    } else {
        draw_nav_cruising_page(state);
    }
}

void hud_ui_draw_modal_overlay(const char *title, const char *message)
{
    // Box boundary from x=4 to 79, y=10 to 42 (corners: 4,10; 79,10; 4,42; 79,42)
    hud_ui_draw_rect(4, 10, 76, 33, 1);
    hud_ui_fill_rect(5, 11, 74, 31, 0);

    if (title && title[0]) {
        char t_buf[20];
        truncate_string_ellipsis(t_buf, title, 11);
        hud_ui_draw_string(7, 13, t_buf, 1);
    }

    hud_ui_draw_line(5, 22, 78, 22, 1);

    if (message && message[0]) {
        char m_buf[20];
        truncate_string_ellipsis(m_buf, message, 11);
        hud_ui_draw_string(7, 26, m_buf, 1);
    }
}

// -----------------------------------------------------------------------------
// 3. Frame Rendering Engine Interface
// -----------------------------------------------------------------------------

void hud_ui_init(void)
{
    ESP_LOGI(TAG, "HUD UI Rendering Engine initialized.");
}

#define BOOT_LOG_MAX_LINES 3
#define BOOT_LOG_LINE_LEN 32
static char s_boot_logs[BOOT_LOG_MAX_LINES][BOOT_LOG_LINE_LEN] = {{0}};
static int s_boot_log_count = 0;
static boot_logo_t s_active_boot_logo = BOOT_LOGO_VARIO;

static void draw_boot_logo_bitmap(const uint8_t *bmp, int16_t x0, int16_t y0)
{
    if (!bmp) return;
    for (int r = 0; r < BOOT_LOGO_HEIGHT; r++) {
        uint32_t row_bits = ((uint32_t)bmp[r * 3] << 16) |
                            ((uint32_t)bmp[r * 3 + 1] << 8) |
                            ((uint32_t)bmp[r * 3 + 2]);
        for (int c = 0; c < BOOT_LOGO_WIDTH; c++) {
            if (row_bits & (1 << (23 - c))) {
                hud_ui_draw_pixel(x0 + c, y0 + r, 1);
            }
        }
    }
}

void hud_ui_boot_init_screen(boot_logo_t logo)
{
    s_active_boot_logo = logo;
    s_boot_log_count = 0;
    memset(s_boot_logs, 0, sizeof(s_boot_logs));

    pcd8544_clear();

    // 1. Draw 20x20 Boot Logo at top-left
    const uint8_t *logo_bmp = boot_logo_get_bitmap((int)logo);
    draw_boot_logo_bitmap(logo_bmp, 2, 1);

    // 2. Draw System / OS title beside logo
    const char *title = boot_logo_get_title((int)logo);
    hud_ui_draw_string(26, 2, title, 1);
    hud_ui_draw_string(26, 11, "Booting...", 1);

    // 3. Separator line at y = 21
    hud_ui_draw_line(0, 21, 83, 21, 1);

    pcd8544_flush();
}

void hud_ui_boot_log(const char *msg)
{
    if (!msg) return;

    ESP_LOGI("BOOT_DMESG", "%s", msg);

    // Shift lines if buffer is full
    if (s_boot_log_count >= BOOT_LOG_MAX_LINES) {
        strncpy(s_boot_logs[0], s_boot_logs[1], BOOT_LOG_LINE_LEN);
        strncpy(s_boot_logs[1], s_boot_logs[2], BOOT_LOG_LINE_LEN);
        strncpy(s_boot_logs[2], msg, BOOT_LOG_LINE_LEN - 1);
        s_boot_logs[2][BOOT_LOG_LINE_LEN - 1] = '\0';
    } else {
        strncpy(s_boot_logs[s_boot_log_count], msg, BOOT_LOG_LINE_LEN - 1);
        s_boot_logs[s_boot_log_count][BOOT_LOG_LINE_LEN - 1] = '\0';
        s_boot_log_count++;
    }

    // Clear console log viewport area (y: 22..47)
    hud_ui_fill_rect(0, 22, 84, 26, 0);

    // Ensure separator line is intact
    hud_ui_draw_line(0, 21, 83, 21, 1);

    // Render active lines in console viewport
    for (int i = 0; i < s_boot_log_count; i++) {
        int16_t y = 23 + (i * 8);
        hud_ui_draw_string(2, y, s_boot_logs[i], 1);
    }

    pcd8544_flush();
}

#define HUD_ALERT_CUE_MS 700

static void draw_cue_incoming_call(const hud_state_t *state, uint32_t elapsed_ms)
{
    // A. Double-line outer border
    hud_ui_draw_rect(0, 0, 84, 48, 1);
    hud_ui_draw_rect(2, 2, 80, 44, 1);

    // B. Top Inverted Alert Banner
    hud_ui_fill_rect(3, 3, 78, 9, 1);
    const char *hdr = "! PANGGILAN MASUK !";
    int16_t hx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(hdr) * 6 - 1)) / 2;
    hud_ui_draw_string(hx, 4, hdr, 0);

    // C. Center Vibrating Phone Graphic with soundwaves
    int8_t wobble = (elapsed_ms / 90) % 2 == 0 ? -2 : 2;
    hud_ui_draw_retro_ringing_phone(42 + wobble, 23, elapsed_ms);
    hud_ui_draw_string(14, 20, "( (", 1);
    hud_ui_draw_string(60, 20, ") )", 1);

    // D. Bottom Caller Preview
    const char *caller = (state->notif_message[0] != '\0') ? state->notif_message :
                         ((state->notif_title[0] != '\0') ? state->notif_title : "Panggilan HP");
    char c_buf[20];
    truncate_string_ellipsis(c_buf, caller, 12);
    int16_t cx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(c_buf) * 6 - 1)) / 2;
    if (cx < 4) cx = 4;
    hud_ui_draw_string(cx, 34, c_buf, 1);
}

static void draw_cue_notification(const hud_state_t *state, uint32_t elapsed_ms)
{
    // A. Outer framed border
    hud_ui_draw_rect(1, 1, 82, 46, 1);
    hud_ui_draw_rect(3, 3, 78, 42, 1);

    // B. Resolve App Name for Dynamic Header & Capsule
    char custom_app[16];
    const char *app = resolve_app_name(state, custom_app, sizeof(custom_app));

    const char *hdr = "! NOTIFIKASI !";
    if (strcmp(app, "GMAPS") == 0 || strcmp(app, "WAZE") == 0) {
        hdr = "! NAVIGASI !";
    } else if (strcmp(app, "PESAN") == 0 || strcmp(app, "SMS") == 0 ||
               strcmp(app, "WHATSAPP") == 0 || strcmp(app, "TELEGRAM") == 0 ||
               strcmp(app, "DISCORD") == 0 || strcmp(app, "LINE") == 0) {
        hdr = "! PESAN MASUK !";
    }

    // Top Inverted Alert Banner
    hud_ui_fill_rect(4, 4, 76, 9, 1);
    int16_t hx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(hdr) * 6 - 1)) / 2;
    hud_ui_draw_string(hx, 5, hdr, 0);

    // C. Center App Badge Capsule
    char app_badge[32];
    snprintf(app_badge, sizeof(app_badge), ">> %s <<", app);
    if (strlen(app_badge) > 13) {
        truncate_string_ellipsis(app_badge, app, 11);
    }
    int16_t ax = (HUD_SCREEN_WIDTH - (int16_t)(strlen(app_badge) * 6 - 1)) / 2;
    if (ax < 4) ax = 4;
    int16_t bw = (strlen(app_badge) * 6) + 4;
    hud_ui_fill_rect(ax - 2, 18, bw, 11, 1);
    hud_ui_draw_string(ax, 20, app_badge, 0);

    // D. Bottom Attention Prompt
    const char *bot = "PERIKSA LAYAR";
    int16_t bx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(bot) * 6 - 1)) / 2;
    hud_ui_draw_string(bx, 34, bot, 1);
}

static void draw_cue_nav_update(const hud_state_t *state, uint32_t elapsed_ms)
{
    // A. Double-line outer border
    hud_ui_draw_rect(0, 0, 84, 48, 1);
    hud_ui_draw_rect(2, 2, 80, 44, 1);

    // B. Top Inverted Alert Banner
    hud_ui_fill_rect(3, 3, 78, 9, 1);
    const char *hdr = ">> BELOKAN DEPAN <<";
    int16_t hx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(hdr) * 6 - 1)) / 2;
    hud_ui_draw_string(hx, 4, hdr, 0);

    // C. Maneuver Direction Preview
    const char *dir_text = (state->nav_directions[0] != '\0') ? state->nav_directions :
                           ((state->nav_title[0] != '\0') ? state->nav_title : "Belok Depan");
    char dir_buf[20];
    truncate_string_ellipsis(dir_buf, dir_text, 12);
    int16_t dx = (HUD_SCREEN_WIDTH - (int16_t)(strlen(dir_buf) * 6 - 1)) / 2;
    if (dx < 4) dx = 4;
    hud_ui_draw_string(dx, 17, dir_buf, 1);

    // D. Distance in Inverted Pill Box
    const char *dist = (state->nav_distance[0] != '\0') ? state->nav_distance : "0 m";
    char dist_crop[16];
    truncate_string_ellipsis(dist_crop, dist, 10);
    char dist_buf[32];
    snprintf(dist_buf, sizeof(dist_buf), "[ %s ]", dist_crop);
    int16_t dist_x = (HUD_SCREEN_WIDTH - (int16_t)(strlen(dist_buf) * 6 - 1)) / 2;
    if (dist_x < 4) dist_x = 4;
    int16_t dist_w = (strlen(dist_buf) * 6) + 4;
    hud_ui_fill_rect(dist_x - 2, 29, dist_w, 12, 1);
    hud_ui_draw_string(dist_x, 32, dist_buf, 0);
}

void hud_ui_render_frame(const hud_state_t *state)
{
    if (!state) return;

    // 1. Clear display buffer
    pcd8544_clear();

    // 1b. Critical Safety Override: Web OTA Flashing Screen
    if (state->ota_in_progress) {
        // Top Inverted Banner (y=0..8, height 9)
        hud_ui_fill_rect(0, 0, 84, 9, 1);
        hud_ui_draw_string(12, 1, "OTA UPDATE", 0);

        // Status text centered at y=11
        hud_ui_draw_string(4, 11, "MENGUPDATE...", 1);

        // Solid, Thick Progress Bar: x=6..77 (w=72, h=8), y=20..27
        hud_ui_draw_rect(6, 20, 72, 8, 1);
        uint8_t bar_w = (uint8_t)((state->ota_progress_pct * 68) / 100);
        if (bar_w > 68) bar_w = 68;
        if (bar_w > 0) hud_ui_fill_rect(8, 22, bar_w, 4, 1);

        // Percentage text centered at y=29
        char pct_buf[16];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", state->ota_progress_pct);
        int16_t px = (HUD_SCREEN_WIDTH - (int16_t)(strlen(pct_buf) * 6 - 1)) / 2;
        hud_ui_draw_string(px, 29, pct_buf, 1);

        // Bottom Inverted Banner (y=37..47, height 11): 14 chars fit perfectly across x=0..82
        hud_ui_fill_rect(0, 37, 84, 11, 1);
        hud_ui_draw_string(0, 39, "JANGAN MATIKAN", 0);

        pcd8544_flush();
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Track Navigation Maneuver Updates
    static bool s_last_nav_active = false;
    static char s_last_nav_dir[64] = {0};
    static bool s_last_nav_near = false;
    static uint32_t s_nav_cue_start_ms = 0;

    if (state->nav_active) {
        bool trigger_nav_cue = false;
        if (!s_last_nav_active) {
            trigger_nav_cue = true; // Navigation newly started
        } else if (state->nav_directions[0] != '\0' &&
                   strncmp(state->nav_directions, s_last_nav_dir, sizeof(s_last_nav_dir)) != 0) {
            trigger_nav_cue = true; // New turn maneuver instruction
        }

        bool is_near = false;
        if (state->nav_distance[0] != '\0') {
            float dval = strtof(state->nav_distance, NULL);
            bool is_km = (strstr(state->nav_distance, "km") != NULL || strstr(state->nav_distance, "KM") != NULL);
            if ((is_km && dval <= 0.10f) || (!is_km && dval <= 100.0f)) {
                is_near = true;
            }
        }
        if (is_near && !s_last_nav_near) {
            trigger_nav_cue = true; // Entered <= 100m warning zone
        }

        if (trigger_nav_cue && state->alert_cue_enabled) {
            s_nav_cue_start_ms = now_ms;
            buzzer_play(BUZZER_TONE_NAV);
        }

        s_last_nav_near = is_near;
        strncpy(s_last_nav_dir, state->nav_directions, sizeof(s_last_nav_dir) - 1);
    } else {
        s_last_nav_near = false;
        s_last_nav_dir[0] = '\0';
        s_nav_cue_start_ms = 0;
    }
    s_last_nav_active = state->nav_active;

    // 2. Autonomous 0-Button State Resolution
    bool show_call = false;
    bool show_notif = false;
    if (state->notification_active) {
        uint32_t duration_ms = 7000;
        hud_ui_get_notif_timing(state, &duration_ms, NULL, NULL);

        if (state->notif_app_id == CHRONOS_APP_RINGER) {
            uint32_t call_timeout_ms = (uint32_t)state->call_timeout_sec * 1000;
            if (call_timeout_ms == 0 || (now_ms >= state->notif_timestamp && (now_ms - state->notif_timestamp < call_timeout_ms))) {
                show_call = true;
            } else {
                // Call timeout expired: auto-dismiss call screen
                static hud_state_t mutable_snap;
                hud_state_get_snapshot(&mutable_snap);
                mutable_snap.notification_active = false;
                hud_state_update(&mutable_snap);
            }
        } else if (now_ms >= state->notif_timestamp && (now_ms - state->notif_timestamp < duration_ms)) {
            // General notification - auto-dismisses after dynamic multi-page duration
            show_notif = true;
        } else {
            // Expired notification: clear active flag in state
            static hud_state_t mutable_snap;
            hud_state_get_snapshot(&mutable_snap);
            mutable_snap.notification_active = false;
            hud_state_update(&mutable_snap);
        }
    }

    // Call tone lifecycle
    static bool s_was_show_call = false;
    if (show_call) {
        static uint32_t s_last_buzz_call_ts = 0;
        if (state->notif_timestamp != s_last_buzz_call_ts) {
            buzzer_play(BUZZER_TONE_CALL);
            s_last_buzz_call_ts = state->notif_timestamp;
        }
    } else if (s_was_show_call) {
        buzzer_stop();
    }
    s_was_show_call = show_call;

    // 3. Render according to priority (Calls & Notifications take full screen, NO NAVBAR)
    bool invert_strobe = false;
    bool cue_active = false;
    uint32_t cue_elapsed = 0;

    if (show_call) {
        uint32_t call_elapsed = (now_ms >= state->notif_timestamp) ? (now_ms - state->notif_timestamp) : 0;
        if (state->alert_cue_enabled && call_elapsed < HUD_ALERT_CUE_MS) {
            cue_active = true;
            cue_elapsed = call_elapsed;
            draw_cue_incoming_call(state, call_elapsed);
            if (call_elapsed < 120 || (call_elapsed >= 200 && call_elapsed < 320)) {
                invert_strobe = true;
            }
        } else {
            hud_ui_draw_incoming_call_overlay(state);
        }
    } else if (show_notif) {
        static uint32_t s_last_buzz_notif_ts = 0;
        if (state->notif_timestamp != s_last_buzz_notif_ts) {
            buzzer_play(BUZZER_TONE_NOTIF);
            s_last_buzz_notif_ts = state->notif_timestamp;
        }

        uint32_t notif_elapsed = (now_ms >= state->notif_timestamp) ? (now_ms - state->notif_timestamp) : 0;
        if (state->alert_cue_enabled && notif_elapsed < HUD_ALERT_CUE_MS) {
            cue_active = true;
            cue_elapsed = notif_elapsed;
            draw_cue_notification(state, notif_elapsed);
            if (notif_elapsed < 120 || (notif_elapsed >= 200 && notif_elapsed < 320)) {
                invert_strobe = true;
            }
        } else {
            hud_ui_draw_notification_overlay(state);
        }
    } else if (state->nav_active) {
        uint32_t nav_elapsed = (s_nav_cue_start_ms > 0 && now_ms >= s_nav_cue_start_ms) ?
                               (now_ms - s_nav_cue_start_ms) : 9999;
        if (state->alert_cue_enabled && nav_elapsed < HUD_ALERT_CUE_MS) {
            cue_active = true;
            cue_elapsed = nav_elapsed;
            draw_cue_nav_update(state, nav_elapsed);
            if (nav_elapsed < 120 || (nav_elapsed >= 200 && nav_elapsed < 320)) {
                invert_strobe = true;
            }
        } else {
            hud_ui_draw_nav_hud(state);
        }
    } else {
        hud_ui_draw_smartwatch_face(state);
    }

    if (invert_strobe) {
        pcd8544_invert_buffer();
    }

    // 4. Backlight Eye-Catch Strobe Synchronizer
    static bool s_was_cue_active = false;
    s_alert_cue_active_flag = cue_active;

    if (cue_active) {
        if (cue_elapsed < 120) {
            pcd8544_set_backlight_raw(255); // Pulse 1: Max Bright
        } else if (cue_elapsed < 200) {
            pcd8544_set_backlight_raw(0);   // Dark flash gap
        } else if (cue_elapsed < 320) {
            pcd8544_set_backlight_raw(255); // Pulse 2: Max Bright
        } else if (cue_elapsed < 420) {
            pcd8544_set_backlight_raw(0);   // Dark flash gap
        } else {
            // Highlight splash alert
            uint8_t alert_bright = state->bl_brightness > 0 ? state->bl_brightness : 200;
            pcd8544_set_backlight_raw(alert_bright);
        }
        s_was_cue_active = true;
    } else if (s_was_cue_active) {
        // Cue ended: cleanly restore ambient/scheduled backlight baseline
        s_was_cue_active = false;
        ldr_sensor_poll_and_update_backlight();
    }

    // 5. Push buffer to PCD8544 hardware via SPI
    pcd8544_flush();
}

