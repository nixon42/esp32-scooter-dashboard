/**
 * @file buzzer.h
 * @brief Optional acoustic buzzer driver for ESP32-C3 SuperMini dashboard.
 * Supports active buzzer (DC level) and passive buzzer (LEDC PWM tone).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_DEFAULT_PIN 20

typedef enum {
    BUZZER_TYPE_ACTIVE = 0,   // Active Buzzer (DC High level drive 3.3V/5V)
    BUZZER_TYPE_PASSIVE = 1,  // Passive Piezo Buzzer (LEDC PWM 2.7 kHz tone)
} buzzer_type_t;

typedef enum {
    BUZZER_PROFILE_MODERN  = 0,  // Modern Chirp (Default)
    BUZZER_PROFILE_SOFT    = 1,  // Soft Chime (Santai / Halus)
    BUZZER_PROFILE_SCIFI   = 2,  // Sci-Fi Arcade (Futuristik)
    BUZZER_PROFILE_MINIMAL = 3,  // Minimal Beep (Sederhana)
    BUZZER_PROFILE_NOKIA   = 4,  // Nokia Classic (Gran Vals & SMS Morse Tone)
    BUZZER_PROFILE_MAX
} buzzer_profile_t;

typedef enum {
    BUZZER_TONE_NONE = 0,
    BUZZER_TONE_BOOT,         // Startup friendly chirp (pip-pip)
    BUZZER_TONE_NOTIF,        // Chat/message double chirp (pip-pip)
    BUZZER_TONE_CALL,         // Ringing cadence (beep-beep-beep)
    BUZZER_TONE_NAV,          // Turn approach warning chime (pip-poop)
    BUZZER_TONE_BEEP,         // Short single beep (100ms)
} buzzer_tone_t;

/**
 * @brief Initialize buzzer driver and background async task.
 * @return ESP_OK on success.
 */
esp_err_t buzzer_init(void);

/**
 * @brief Reconfigure buzzer hardware pin, type, volume, and profile dynamically.
 * @param gpio_pin GPIO pin number (e.g. 20, 21, 9)
 * @param type Buzzer hardware type (Active or Passive PWM)
 * @param enabled True if buzzer audio is enabled
 * @param volume_pct Volume percentage (10..100 %)
 * @param profile Sound melody theme profile (Modern, Soft, Sci-Fi, Minimal)
 */
void buzzer_configure(uint8_t gpio_pin, buzzer_type_t type, bool enabled, uint8_t volume_pct, buzzer_profile_t profile);

/**
 * @brief Set buzzer volume percentage (10..100%).
 */
void buzzer_set_volume(uint8_t volume_pct);

/**
 * @brief Set buzzer sound melody theme profile.
 */
void buzzer_set_profile(buzzer_profile_t profile);

/**
 * @brief Configure individual event audio enable flags.
 * @param boot_en True to enable boot up jingle when ignition turns ON
 * @param notif_en True to enable message/chat notification double chirp
 * @param call_en True to enable incoming call ringing melody
 * @param nav_en True to enable navigation turn warning chime
 */
void buzzer_set_event_enables(bool boot_en, bool notif_en, bool call_en, bool nav_en);

/**
 * @brief Asynchronously play a designated audio pattern.
 * @param tone Tone pattern enum
 */
void buzzer_play(buzzer_tone_t tone);

/**
 * @brief Immediately stop any active buzzer audio.
 */
void buzzer_stop(void);

#ifdef __cplusplus
}
#endif
