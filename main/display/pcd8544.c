/**
 * @file pcd8544.c
 * @brief Native ESP-IDF PCD8544 (Nokia 5110) Hardware SPI Master & LEDC Backlight Driver.
 * Target: ESP32-C3 SuperMini (Honda Vario 125 K60)
 */

#include "pcd8544.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "PCD8544";

// Static Driver State
static spi_device_handle_t s_spi_dev = NULL;
static SemaphoreHandle_t   s_pcd8544_mutex = NULL;
static SemaphoreHandle_t   s_bl_mutex = NULL;
static uint8_t             s_framebuffer[PCD8544_BUFFER_SIZE];
static uint8_t             s_prev_framebuffer[PCD8544_BUFFER_SIZE];
static bool                s_force_flush = true;
static uint8_t             s_current_contrast = PCD8544_CONTRAST_DEF;
static uint8_t             s_current_duty_pct = 0;
static uint8_t             s_current_raw_duty = 0;
static bool                s_backlight_inverted = true; // Nokia 5110 Red PCB (HXE) uses active-low BL pin
static bool                s_fade_installed = false;

// SPI Pre-Transfer Callback for DC Line
static void IRAM_ATTR pcd8544_pre_transfer_cb(spi_transaction_t *t)
{
    int dc_level = (int)(intptr_t)t->user;
    gpio_set_level(PCD8544_PIN_DC, dc_level);
}

// Low-Level Synchronous Command Transmission
static esp_err_t pcd8544_write_cmd(uint8_t cmd)
{
    if (!s_spi_dev) return ESP_ERR_INVALID_STATE;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void *)(intptr_t)0; // DC = 0 (Command)

    gpio_set_level(PCD8544_PIN_DC, 0);
    return spi_device_polling_transmit(s_spi_dev, &t);
}

// Low-Level Synchronous Framebuffer Data Transmission
static esp_err_t pcd8544_write_data(const uint8_t *data, size_t length)
{
    if (!s_spi_dev || !data || length == 0) return ESP_ERR_INVALID_STATE;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = length * 8;
    t.tx_buffer = data;
    t.user = (void *)(intptr_t)1; // DC = 1 (Data)

    gpio_set_level(PCD8544_PIN_DC, 1);
    return spi_device_polling_transmit(s_spi_dev, &t);
}

// Backlight Math Utilities
static inline uint32_t pct_to_raw_duty(uint8_t pct)
{
    if (pct == 0) return 0;
    if (pct >= 100) return 255;
    return ((uint32_t)pct * 255 + 50) / 100;
}

static inline uint8_t raw_duty_to_pct(uint32_t duty)
{
    if (duty == 0) return 0;
    if (duty >= 255) return 100;
    return (uint8_t)(((duty * 100) + 127) / 255);
}

static inline uint32_t apply_polarity(uint32_t raw_duty)
{
    if (s_backlight_inverted) {
        if (raw_duty == 0) return 256;      // Constant HIGH (3.3V) -> Active Low LED completely OFF
        if (raw_duty >= 255) return 0;      // Constant LOW (GND) -> Active Low LED 100% full ON
        return 256 - raw_duty;
    } else {
        if (raw_duty == 0) return 0;        // Constant LOW (0V) -> Active High LED completely OFF
        if (raw_duty >= 255) return 256;    // Constant HIGH (3.3V) -> Active High LED 100% full ON
        return raw_duty;
    }
}

// Backlight Hardware Initializer
static void pcd8544_init_backlight(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = PCD8544_BACKLIGHT_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    uint32_t initial_hw_duty = apply_polarity(0);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PCD8544_PIN_BL,
        .duty           = initial_hw_duty,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    if (!s_fade_installed) {
        esp_err_t err = ledc_fade_func_install(0);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_fade_installed = true;
        }
    }
}

esp_err_t pcd8544_init(void)
{
    ESP_LOGI(TAG, "Initializing PCD8544 Hardware SPI Master driver...");

    if (!s_pcd8544_mutex) {
        s_pcd8544_mutex = xSemaphoreCreateMutex();
        if (!s_pcd8544_mutex) return ESP_ERR_NO_MEM;
    }
    if (!s_bl_mutex) {
        s_bl_mutex = xSemaphoreCreateMutex();
        if (!s_bl_mutex) return ESP_ERR_NO_MEM;
    }

    // 1. Configure DC and RST GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PCD8544_PIN_DC) | (1ULL << PCD8544_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // 2. Configure SPI Bus
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PCD8544_PIN_MOSI,
        .sclk_io_num = PCD8544_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PCD8544_BUFFER_SIZE + 8,
    };
    esp_err_t ret = spi_bus_initialize(PCD8544_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Attach PCD8544 Device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = PCD8544_SPI_CLOCK_HZ,
        .mode = 0, // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = PCD8544_PIN_CS,
        .queue_size = 2,
        .pre_cb = pcd8544_pre_transfer_cb,
    };
    ret = spi_bus_add_device(PCD8544_SPI_HOST, &devcfg, &s_spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Initialize Backlight PWM
    pcd8544_init_backlight();

    // 5. Hardware Reset Pulse (GPIO 10)
    gpio_set_level(PCD8544_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(PCD8544_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(15));

    // 6. Issue Initialization Command Sequence
    if (xSemaphoreTake(s_pcd8544_mutex, portMAX_DELAY) == pdTRUE) {
        pcd8544_write_cmd(0x21);                     // Extended Instruction Set
        pcd8544_write_cmd(0x06);                     // Temperature Coefficient = 2
        pcd8544_write_cmd(0x14);                     // Bias System 1:48 (n = 4)
        pcd8544_write_cmd(0x80 | s_current_contrast); // Set Contrast Vop
        pcd8544_write_cmd(0x20);                     // Basic Instruction Set
        pcd8544_write_cmd(0x0C);                     // Display Control: Normal Mode
        xSemaphoreGive(s_pcd8544_mutex);
    }

    // 7. Clear Framebuffer and initial screen flush
    pcd8544_clear();
    pcd8544_flush();

    ESP_LOGI(TAG, "PCD8544 hardware initialization complete.");
    return ESP_OK;
}

bool pcd8544_is_valid_contrast(uint8_t contrast)
{
    return (contrast >= PCD8544_CONTRAST_MIN && contrast <= PCD8544_CONTRAST_MAX);
}

void pcd8544_set_contrast(uint8_t contrast)
{
    if (contrast < PCD8544_CONTRAST_MIN) {
        contrast = PCD8544_CONTRAST_MIN;
    } else if (contrast > PCD8544_CONTRAST_MAX) {
        contrast = PCD8544_CONTRAST_MAX;
    }
    s_current_contrast = contrast;
    s_force_flush = true;

    if (s_pcd8544_mutex && xSemaphoreTake(s_pcd8544_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pcd8544_write_cmd(0x21);              // Switch to extended instructions
        pcd8544_write_cmd(0x80 | contrast);   // Set Vop
        pcd8544_write_cmd(0x20);              // Switch back to basic instructions
        xSemaphoreGive(s_pcd8544_mutex);
    }
}

uint8_t pcd8544_get_contrast(void)
{
    return s_current_contrast;
}

void pcd8544_set_backlight(uint8_t duty_pct)
{
    if (duty_pct > 100) duty_pct = 100;

    if (s_bl_mutex && xSemaphoreTake(s_bl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        uint32_t raw_duty = pct_to_raw_duty(duty_pct);
        uint32_t hw_duty  = apply_polarity(raw_duty);

        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty, 0);

        s_current_duty_pct = duty_pct;
        s_current_raw_duty = (uint8_t)raw_duty;

        xSemaphoreGive(s_bl_mutex);
    }
}

void pcd8544_set_backlight_raw(uint8_t raw_duty)
{
    if (s_bl_mutex && xSemaphoreTake(s_bl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        uint32_t hw_duty = apply_polarity(raw_duty);
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty, 0);

        s_current_raw_duty = raw_duty;
        s_current_duty_pct = raw_duty_to_pct(raw_duty);

        xSemaphoreGive(s_bl_mutex);
    }
}

void pcd8544_set_backlight_fade(uint8_t target_duty_pct, uint32_t fade_ms)
{
    if (target_duty_pct > 100) target_duty_pct = 100;

    if (fade_ms == 0 || !s_fade_installed) {
        pcd8544_set_backlight(target_duty_pct);
        return;
    }

    if (s_bl_mutex && xSemaphoreTake(s_bl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        uint32_t raw_target = pct_to_raw_duty(target_duty_pct);
        uint32_t hw_target  = apply_polarity(raw_target);

        esp_err_t err = ledc_set_fade_with_time(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            hw_target,
            (int)fade_ms
        );

        if (err == ESP_OK) {
            err = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
        }

        if (err != ESP_OK) {
            ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_target, 0);
        }

        s_current_duty_pct = target_duty_pct;
        s_current_raw_duty = (uint8_t)raw_target;

        xSemaphoreGive(s_bl_mutex);
    }
}

void pcd8544_set_backlight_inverted(bool inverted)
{
    if (s_bl_mutex && xSemaphoreTake(s_bl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_backlight_inverted != inverted) {
            s_backlight_inverted = inverted;
            uint32_t hw_duty = apply_polarity(s_current_raw_duty);
            ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty, 0);
        }
        xSemaphoreGive(s_bl_mutex);
    } else {
        s_backlight_inverted = inverted;
    }
}

uint8_t pcd8544_get_backlight_duty_pct(void)
{
    return s_current_duty_pct;
}

uint8_t pcd8544_get_backlight_raw_duty(void)
{
    return s_current_raw_duty;
}

void pcd8544_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void pcd8544_flush(void)
{
    if (s_pcd8544_mutex && xSemaphoreTake(s_pcd8544_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Dirty-buffer check: Skip redundant SPI transactions if frame is identical to previous
        if (!s_force_flush && memcmp(s_framebuffer, s_prev_framebuffer, PCD8544_BUFFER_SIZE) == 0) {
            xSemaphoreGive(s_pcd8544_mutex);
            return;
        }

        // Reset column X and bank Y address pointers to (0, 0)
        pcd8544_write_cmd(0x80); // X = 0
        pcd8544_write_cmd(0x40); // Y = 0

        // Transmit entire 504-byte buffer in single polling transaction
        pcd8544_write_data(s_framebuffer, PCD8544_BUFFER_SIZE);

        memcpy(s_prev_framebuffer, s_framebuffer, PCD8544_BUFFER_SIZE);
        s_force_flush = false;

        xSemaphoreGive(s_pcd8544_mutex);
    }
}

void pcd8544_force_flush(void)
{
    s_force_flush = true;
    pcd8544_flush();
}

void pcd8544_set_pixel(int x, int y, bool color)
{
    if (x < 0 || x >= PCD8544_WIDTH || y < 0 || y >= PCD8544_HEIGHT) {
        return;
    }
    int bank = y / 8;
    int bit = y % 8;
    int idx = bank * PCD8544_WIDTH + x;

    if (color) {
        s_framebuffer[idx] |= (1 << bit);
    } else {
        s_framebuffer[idx] &= ~(1 << bit);
    }
}

bool pcd8544_get_pixel(int x, int y)
{
    if (x < 0 || x >= PCD8544_WIDTH || y < 0 || y >= PCD8544_HEIGHT) {
        return false;
    }
    int bank = y / 8;
    int bit = y % 8;
    int idx = bank * PCD8544_WIDTH + x;
    return (s_framebuffer[idx] & (1 << bit)) != 0;
}

uint8_t* pcd8544_get_framebuffer(void)
{
    return s_framebuffer;
}

void pcd8544_invert_buffer(void)
{
    for (size_t i = 0; i < PCD8544_BUFFER_SIZE; i++) {
        s_framebuffer[i] ^= 0xFF;
    }
}
