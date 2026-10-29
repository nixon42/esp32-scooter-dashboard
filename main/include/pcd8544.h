/**
 * @file pcd8544.h
 * @brief Native ESP-IDF PCD8544 (Nokia 5110) Hardware SPI Master & LEDC Backlight Driver.
 * Target: ESP32-C3 SuperMini (Honda Vario 125 K60)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display Geometry Specifications
#define PCD8544_WIDTH               84
#define PCD8544_HEIGHT              48
#define PCD8544_BANKS               6
#define PCD8544_BUFFER_SIZE         (PCD8544_WIDTH * PCD8544_BANKS) // 504 Bytes

// Hardware Pinout Definitions
#define PCD8544_PIN_SCK             GPIO_NUM_6
#define PCD8544_PIN_MOSI            GPIO_NUM_7
#define PCD8544_PIN_CS              GPIO_NUM_8
#define PCD8544_PIN_DC              GPIO_NUM_4
#define PCD8544_PIN_RST             GPIO_NUM_10
#define PCD8544_PIN_BL              GPIO_NUM_2

// Contrast Boundary Limits
#define PCD8544_CONTRAST_MIN        0x38  // Decimal 56
#define PCD8544_CONTRAST_MAX        0x48  // Decimal 72
#define PCD8544_CONTRAST_DEF        0x3F  // Decimal 63

// Hardware Bus Parameters
#define PCD8544_SPI_HOST            SPI2_HOST
#define PCD8544_SPI_CLOCK_HZ        (4 * 1000 * 1000) // 4.0 MHz
#define PCD8544_BACKLIGHT_FREQ_HZ   5000              // 5 kHz PWM

// Public Display APIs
esp_err_t pcd8544_init(void);
void      pcd8544_set_contrast(uint8_t contrast);
uint8_t   pcd8544_get_contrast(void);
bool      pcd8544_is_valid_contrast(uint8_t contrast);
void      pcd8544_clear(void);
void      pcd8544_flush(void);
void      pcd8544_force_flush(void);

// Backlight APIs
void      pcd8544_set_backlight(uint8_t duty_pct);
void      pcd8544_set_backlight_raw(uint8_t raw_duty);
void      pcd8544_set_backlight_fade(uint8_t target_duty_pct, uint32_t fade_ms);
void      pcd8544_set_backlight_inverted(bool inverted);
uint8_t   pcd8544_get_backlight_duty_pct(void);
uint8_t   pcd8544_get_backlight_raw_duty(void);

// Framebuffer & Pixel Primitives
void      pcd8544_set_pixel(int x, int y, bool color);
bool      pcd8544_get_pixel(int x, int y);
uint8_t*  pcd8544_get_framebuffer(void);
void      pcd8544_invert_buffer(void);

#ifdef __cplusplus
}
#endif
