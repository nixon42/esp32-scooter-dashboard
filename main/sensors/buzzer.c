/**
 * @file buzzer.c
 * @brief Optional acoustic buzzer driver for ESP32-C3 SuperMini dashboard.
 */

#include "buzzer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

#define BUZZER_LEDC_TIMER    LEDC_TIMER_1
#define BUZZER_LEDC_CHANNEL  LEDC_CHANNEL_1
#define BUZZER_LEDC_MODE     LEDC_LOW_SPEED_MODE

static QueueHandle_t s_buzzer_queue = NULL;
static uint8_t s_current_pin = BUZZER_DEFAULT_PIN;
static buzzer_type_t s_current_type = BUZZER_TYPE_ACTIVE;
static bool s_enabled = false;
static uint8_t s_volume = 80; // Default volume: 80% (range 10..100)
static buzzer_profile_t s_profile = BUZZER_PROFILE_MODERN;
static volatile bool s_stop_requested = false;
static bool s_hw_configured = false;
static bool s_boot_en = true;
static bool s_notif_en = true;
static bool s_call_en = true;
static bool s_nav_en = true;

static void hw_silence(void)
{
    if (!s_hw_configured) return;
    if (s_current_type == BUZZER_TYPE_PASSIVE) {
        ledc_set_duty_and_update(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0, 0);
    } else {
        gpio_set_level(s_current_pin, 0);
    }
}

static void hw_beep(uint32_t freq_hz)
{
    if (!s_hw_configured || !s_enabled) return;
    if (s_current_type == BUZZER_TYPE_PASSIVE) {
        if (freq_hz > 0) {
            ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
        }
        // Map volume percentage (0..100) to LEDC 8-bit duty (0..128 max for 50% square wave)
        uint32_t duty = (s_volume * 128) / 100;
        if (duty < 4 && s_volume > 0) duty = 4;
        ledc_set_duty_and_update(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty, 0);
    } else {
        gpio_set_level(s_current_pin, 1);
    }
}

static void setup_hardware(void)
{
    hw_silence();

    if (!s_enabled) {
        if (s_hw_configured) {
            gpio_reset_pin(s_current_pin);
            s_hw_configured = false;
        }
        return;
    }

    if (s_current_type == BUZZER_TYPE_PASSIVE) {
        ledc_timer_config_t timer_conf = {
            .speed_mode       = BUZZER_LEDC_MODE,
            .duty_resolution  = LEDC_TIMER_8_BIT,
            .timer_num        = BUZZER_LEDC_TIMER,
            .freq_hz          = 2700, // 2.7 kHz default resonant frequency
            .clk_cfg          = LEDC_AUTO_CLK
        };
        ledc_timer_config(&timer_conf);

        ledc_channel_config_t ch_conf = {
            .gpio_num       = s_current_pin,
            .speed_mode     = BUZZER_LEDC_MODE,
            .channel        = BUZZER_LEDC_CHANNEL,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = BUZZER_LEDC_TIMER,
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&ch_conf);
    } else {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_current_pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(s_current_pin, 0);
    }

    s_hw_configured = true;
    ESP_LOGI(TAG, "Buzzer configured on GPIO %d (Type: %s, Enabled: %d)",
             s_current_pin, (s_current_type == BUZZER_TYPE_PASSIVE) ? "Passive PWM" : "Active DC", s_enabled);
}

static void play_delay_check(uint32_t ms)
{
    uint32_t elapsed = 0;
    while (elapsed < ms && !s_stop_requested) {
        uint32_t step = (ms - elapsed > 20) ? 20 : (ms - elapsed);
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
}

typedef struct {
    uint16_t freq;     // Frequency in Hz (LEDC PWM)
    uint16_t dur_ms;   // Note duration in ms
    uint16_t pause_ms; // Silent pause after note in ms
} buzzer_note_step_t;

static void play_melody(const buzzer_note_step_t *notes, size_t count)
{
    for (size_t i = 0; i < count && !s_stop_requested; i++) {
        if (notes[i].freq > 0) {
            hw_beep(notes[i].freq);
        } else {
            hw_silence();
        }
        play_delay_check(notes[i].dur_ms);
        hw_silence();
        if (notes[i].pause_ms > 0) {
            play_delay_check(notes[i].pause_ms);
        }
    }
}

// Nokia Classic melodies:
// Boot: Opening 4-note phrase of iconic Gran Vals (Nokia Tune)
static const buzzer_note_step_t s_nokia_boot[] = {
    { 1319, 130, 25 }, // E6
    { 1175, 130, 25 }, // D6
    { 1480, 200, 25 }, // F#6
    { 1661, 240, 0  }, // G#6
};

// Notif: Iconic Nokia "Special" SMS tone (Morse code for SMS: "... -- ...")
static const buzzer_note_step_t s_nokia_sms[] = {
    { 2700,  50, 40 }, // dit
    { 2700,  50, 40 }, // dit
    { 2700,  50, 130 },// dit (letter space)
    { 2700, 140, 50 }, // dah
    { 2700, 140, 130 },// dah (letter space)
    { 2700,  50, 40 }, // dit
    { 2700,  50, 40 }, // dit
    { 2700,  50, 0   }, // dit
};

// Nav: Ascending Nokia connection / cue prompt
static const buzzer_note_step_t s_nokia_nav[] = {
    { 1175,  75, 25 }, // D6
    { 1480,  75, 25 }, // F#6
    { 2217, 130, 0  }, // C#7
};

// Call: The Full Legendary Nokia Tune (Gran Vals by Francisco Tárrega)
static const buzzer_note_step_t s_nokia_tune[] = {
    { 1319, 130, 25 }, // E6
    { 1175, 130, 25 }, // D6
    { 1480, 240, 25 }, // F#6
    { 1661, 240, 25 }, // G#6
    { 2217, 130, 25 }, // C#7
    { 1976, 130, 25 }, // B6
    { 1175, 240, 25 }, // D6
    { 1319, 240, 25 }, // E6
    { 1976, 130, 25 }, // B6
    { 1760, 130, 25 }, // A6
    { 1109, 240, 25 }, // C#6
    { 1319, 240, 25 }, // E6
    { 1760, 480, 0  }, // A6
};

static void buzzer_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Buzzer async worker task started.");
    buzzer_tone_t tone;

    while (1) {
        if (xQueueReceive(s_buzzer_queue, &tone, portMAX_DELAY) == pdTRUE) {
            s_stop_requested = false;

            if (!s_enabled || tone == BUZZER_TONE_NONE) {
                hw_silence();
                continue;
            }

            switch (tone) {
                case BUZZER_TONE_BEEP:
                    if (s_profile == BUZZER_PROFILE_SOFT) {
                        hw_beep(2000);
                        play_delay_check(80);
                    } else if (s_profile == BUZZER_PROFILE_SCIFI) {
                        hw_beep(3300);
                        play_delay_check(50);
                    } else if (s_profile == BUZZER_PROFILE_NOKIA) {
                        hw_beep(2700);
                        play_delay_check(45);
                    } else {
                        hw_beep(2700);
                        play_delay_check(70);
                    }
                    hw_silence();
                    break;

                case BUZZER_TONE_BOOT:
                    if (s_profile == BUZZER_PROFILE_NOKIA) {
                        // Nokia Handshake: opening phrase of Gran Vals
                        play_melody(s_nokia_boot, sizeof(s_nokia_boot) / sizeof(s_nokia_boot[0]));
                    } else if (s_profile == BUZZER_PROFILE_SOFT) {
                        // Soft elegant chime: 1750 Hz -> 2200 Hz
                        hw_beep(1750);
                        play_delay_check(80);
                        hw_silence();
                        play_delay_check(50);
                        hw_beep(2200);
                        play_delay_check(140);
                    } else if (s_profile == BUZZER_PROFILE_SCIFI) {
                        // Sci-Fi tri-tone ascending arpeggio: 2000 -> 2600 -> 3400 Hz
                        hw_beep(2000);
                        play_delay_check(45);
                        hw_silence();
                        play_delay_check(25);
                        hw_beep(2600);
                        play_delay_check(45);
                        hw_silence();
                        play_delay_check(25);
                        hw_beep(3400);
                        play_delay_check(110);
                    } else if (s_profile == BUZZER_PROFILE_MINIMAL) {
                        // Minimal single confirmation tone
                        hw_beep(2700);
                        play_delay_check(100);
                    } else {
                        // Modern Chirp (Default)
                        hw_beep(2400);
                        play_delay_check(60);
                        hw_silence();
                        play_delay_check(40);
                        hw_beep(3200);
                        play_delay_check(100);
                    }
                    hw_silence();
                    break;

                case BUZZER_TONE_NOTIF:
                    if (s_profile == BUZZER_PROFILE_NOKIA) {
                        // Iconic Nokia "Special" SMS tone ("... -- ...")
                        play_melody(s_nokia_sms, sizeof(s_nokia_sms) / sizeof(s_nokia_sms[0]));
                    } else if (s_profile == BUZZER_PROFILE_SOFT) {
                        // Soft two-tone chime
                        hw_beep(2000);
                        play_delay_check(70);
                        hw_silence();
                        play_delay_check(40);
                        hw_beep(2400);
                        play_delay_check(90);
                    } else if (s_profile == BUZZER_PROFILE_SCIFI) {
                        // Rapid video-game coin / tri-chirp
                        hw_beep(2600);
                        play_delay_check(35);
                        hw_silence();
                        play_delay_check(20);
                        hw_beep(3100);
                        play_delay_check(35);
                        hw_silence();
                        play_delay_check(20);
                        hw_beep(3700);
                        play_delay_check(60);
                    } else if (s_profile == BUZZER_PROFILE_MINIMAL) {
                        // Minimal single beep
                        hw_beep(2700);
                        play_delay_check(80);
                    } else {
                        // Modern double chirp
                        hw_beep(3000);
                        play_delay_check(70);
                        hw_silence();
                        play_delay_check(40);
                        hw_beep(3000);
                        play_delay_check(80);
                    }
                    hw_silence();
                    break;

                case BUZZER_TONE_NAV:
                    if (s_profile == BUZZER_PROFILE_NOKIA) {
                        // Ascending Nokia prompt cue
                        play_melody(s_nokia_nav, sizeof(s_nokia_nav) / sizeof(s_nokia_nav[0]));
                    } else if (s_profile == BUZZER_PROFILE_SOFT) {
                        // Soft turn prompt
                        hw_beep(1800);
                        play_delay_check(100);
                        hw_silence();
                        play_delay_check(40);
                        hw_beep(2200);
                        play_delay_check(150);
                    } else if (s_profile == BUZZER_PROFILE_SCIFI) {
                        // Sci-Fi sweep
                        hw_beep(2200);
                        play_delay_check(50);
                        hw_silence();
                        play_delay_check(20);
                        hw_beep(2800);
                        play_delay_check(50);
                        hw_silence();
                        play_delay_check(20);
                        hw_beep(3500);
                        play_delay_check(110);
                    } else if (s_profile == BUZZER_PROFILE_MINIMAL) {
                        // Minimal turn beep
                        hw_beep(2700);
                        play_delay_check(120);
                    } else {
                        // Modern turn chime
                        hw_beep(2400);
                        play_delay_check(90);
                        hw_silence();
                        play_delay_check(40);
                        hw_beep(3200);
                        play_delay_check(140);
                    }
                    hw_silence();
                    break;

                case BUZZER_TONE_CALL:
                    // Ring cadence until cancelled or timeout
                    while (!s_stop_requested) {
                        if (s_profile == BUZZER_PROFILE_NOKIA) {
                            // The Full Legendary Nokia Tune (Gran Vals)
                            play_melody(s_nokia_tune, sizeof(s_nokia_tune) / sizeof(s_nokia_tune[0]));
                            play_delay_check(1400);
                        } else if (s_profile == BUZZER_PROFILE_SOFT) {
                            hw_beep(2100);
                            play_delay_check(200);
                            hw_silence();
                            play_delay_check(100);
                            hw_beep(2100);
                            play_delay_check(200);
                            hw_silence();
                            play_delay_check(1400);
                        } else if (s_profile == BUZZER_PROFILE_SCIFI) {
                            // Alternating warp alarm
                            hw_beep(2400);
                            play_delay_check(90);
                            hw_silence();
                            play_delay_check(40);
                            hw_beep(3300);
                            play_delay_check(90);
                            hw_silence();
                            play_delay_check(40);
                            hw_beep(2400);
                            play_delay_check(90);
                            hw_silence();
                            play_delay_check(40);
                            hw_beep(3300);
                            play_delay_check(150);
                            hw_silence();
                            play_delay_check(1100);
                        } else if (s_profile == BUZZER_PROFILE_MINIMAL) {
                            hw_beep(2700);
                            play_delay_check(250);
                            hw_silence();
                            play_delay_check(250);
                            hw_beep(2700);
                            play_delay_check(250);
                            hw_silence();
                            play_delay_check(1200);
                        } else {
                            // Modern retro phone ring
                            hw_beep(2700);
                            play_delay_check(150);
                            hw_silence();
                            play_delay_check(80);
                            hw_beep(2700);
                            play_delay_check(150);
                            hw_silence();
                            play_delay_check(80);
                            hw_beep(2700);
                            play_delay_check(300);
                            hw_silence();
                            play_delay_check(1200);
                        }

                        // Check if a new tone superseded or stop requested
                        buzzer_tone_t next_tone;
                        if (xQueuePeek(s_buzzer_queue, &next_tone, 0) == pdTRUE) {
                            break;
                        }
                    }
                    hw_silence();
                    break;

                default:
                    hw_silence();
                    break;
            }

            hw_silence();
        }
    }
}

esp_err_t buzzer_init(void)
{
    if (!s_buzzer_queue) {
        s_buzzer_queue = xQueueCreate(4, sizeof(buzzer_tone_t));
        if (!s_buzzer_queue) return ESP_ERR_NO_MEM;
    }

    xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 4, NULL);
    return ESP_OK;
}

void buzzer_configure(uint8_t gpio_pin, buzzer_type_t type, bool enabled, uint8_t volume_pct, buzzer_profile_t profile)
{
    s_current_pin = gpio_pin;
    s_current_type = type;
    s_enabled = enabled;
    s_volume = (volume_pct < 10) ? 10 : ((volume_pct > 100) ? 100 : volume_pct);
    s_profile = (profile < BUZZER_PROFILE_MAX) ? profile : BUZZER_PROFILE_MODERN;
    setup_hardware();
}

void buzzer_set_volume(uint8_t volume_pct)
{
    s_volume = (volume_pct < 10) ? 10 : ((volume_pct > 100) ? 100 : volume_pct);
}

void buzzer_set_profile(buzzer_profile_t profile)
{
    s_profile = (profile < BUZZER_PROFILE_MAX) ? profile : BUZZER_PROFILE_MODERN;
}

void buzzer_set_event_enables(bool boot_en, bool notif_en, bool call_en, bool nav_en)
{
    s_boot_en = boot_en;
    s_notif_en = notif_en;
    s_call_en = call_en;
    s_nav_en = nav_en;
}

void buzzer_play(buzzer_tone_t tone)
{
    if (!s_enabled || !s_buzzer_queue) return;
    if (tone == BUZZER_TONE_BOOT && !s_boot_en) return;
    if (tone == BUZZER_TONE_NOTIF && !s_notif_en) return;
    if (tone == BUZZER_TONE_CALL && !s_call_en) return;
    if (tone == BUZZER_TONE_NAV && !s_nav_en) return;
    s_stop_requested = false;
    xQueueSend(s_buzzer_queue, &tone, 0);
}

void buzzer_stop(void)
{
    s_stop_requested = true;
    hw_silence();
    if (s_buzzer_queue) {
        buzzer_tone_t none = BUZZER_TONE_NONE;
        xQueueSend(s_buzzer_queue, &none, 0);
    }
}
