/**
 * @file hud_ui.h
 * @brief HUD UI Rendering Engine for Nokia 5110 (PCD8544) 84x48 LCD.
 * Target: ESP32-C3 SuperMini / Honda Vario 125 K60
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "hud_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUD_SCREEN_WIDTH        84
#define HUD_SCREEN_HEIGHT       48
#define HUD_STATUS_BAR_HEIGHT   9
#define HUD_MAIN_AREA_Y         9
#define HUD_MAIN_AREA_HEIGHT    39

/**
 * @brief Initialize HUD UI engine.
 */
void hud_ui_init(void);

/**
 * @brief Initialize and display real-time boot screen with selected logo.
 * @param logo Boot logo enum.
 */
void hud_ui_boot_init_screen(boot_logo_t logo);

/**
 * @brief Append a real-time debug step to the Linux-style console on LCD.
 * @param msg Log message line (e.g. "[ OK ] NVS ready").
 */
void hud_ui_boot_log(const char *msg);

/**
 * @brief Render a complete telemetry frame into PCD8544 framebuffer and flush to LCD.
 * @param state Telemetry state snapshot.
 */
void hud_ui_render_frame(const hud_state_t *state);

/**
 * @brief Drawing Primitives
 */
void hud_ui_draw_pixel(int16_t x, int16_t y, uint8_t color);
void hud_ui_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void hud_ui_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void hud_ui_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void hud_ui_draw_char_5x7(int16_t x, int16_t y, char c, uint8_t color);
void hud_ui_draw_string(int16_t x, int16_t y, const char *str, uint8_t color);
void hud_ui_draw_large_digit(int16_t x, int16_t y, char digit, uint8_t color);
void hud_ui_draw_ble_glyph(int16_t x, int16_t y, uint8_t color);
void hud_ui_blit_chronos_icon(const uint8_t *icon_bmp, int16_t dest_x, int16_t dest_y);
void hud_ui_blit_chronos_icon_24x24(const uint8_t *icon_bmp, int16_t dest_x, int16_t dest_y);

/**
 * @brief Subsystem rendering functions.
 */
void hud_ui_draw_status_bar(const hud_state_t *state);
void hud_ui_draw_global_header(const hud_state_t *state);
void hud_ui_draw_smartwatch_face(const hud_state_t *state);
void hud_ui_draw_multitask_nav(const hud_state_t *state);
void hud_ui_draw_incoming_call_overlay(const hud_state_t *state);
void hud_ui_draw_notification_overlay(const hud_state_t *state);

void hud_ui_draw_cruising_hud(const hud_state_t *state);
void hud_ui_draw_nav_hud(const hud_state_t *state);
void hud_ui_draw_modal_overlay(const char *title, const char *message);

/**
 * @brief Check if an eye-catchy alert cue is currently playing (to avoid backlight conflicts).
 * @return true if alert cue strobe is active.
 */
bool hud_ui_is_alert_cue_active(void);

#ifdef __cplusplus
}
#endif
