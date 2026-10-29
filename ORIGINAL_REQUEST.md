# Original User Request

## 2026-10-05T12:16:57Z

DIY Smart Motorcycle HUD & Telemetry Dashboard native firmware based on ESP-IDF (v5.x) for Honda Vario 125 Old LED (K60) using ESP32-C3 SuperMini and Nokia 5110 LCD (PCD8544). Includes Apache NimBLE integration with the Android Chronos companion app, modular optional hardware sensors (Aki Voltmeter ADC, VSS Speedometer GPIO ISR, DHT11/DHT22 Ambient Temperature Sensor, and LDR Light Sensor for Auto Backlight) with automatic BLE GPS speed fallback, and an embedded single-page Web Configuration & Web OTA update portal.

Working directory: `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard`
Integrity mode: development

Reference documentation:
- Chronos ESP32 BLE API & Protocol: https://github.com/fbiego/chronos-esp32/blob/main/api.md

## Complete Pin Assignment (ESP32-C3 SuperMini)
- **GPIO 0 (ADC1_CH0):** Aki Voltmeter divider (10kΩ + 2.2kΩ + 3.3V Zener) [OPTIONAL]
- **GPIO 1 (ADC1_CH1):** LDR Light Sensor divider (LDR + 10kΩ to GND) for Auto Backlight [OPTIONAL]
- **GPIO 2 (LEDC PWM):** Nokia 5110 Backlight control (PWM brightness dimming)
- **GPIO 3 (GPIO):** DHT11/DHT22 Ambient Temperature & Humidity Sensor (1-wire) [OPTIONAL]
- **GPIO 4 (GPIO):** Nokia 5110 DC (Data / Command)
- **GPIO 5 (GPIO ISR):** Speedometer VSS pulse interrupt (Vario pink/green wire) [OPTIONAL]
- **GPIO 6 (SPI SCK):** Nokia 5110 SCK / SCLK
- **GPIO 7 (SPI MOSI):** Nokia 5110 MOSI / DIN
- **GPIO 8 (GPIO):** Nokia 5110 CS / CE (Chip Select)
- **GPIO 10 (GPIO):** Nokia 5110 RST (Reset)
- **GPIO 20 / 21:** Dedicated USB/UART serial monitor and programming

## Requirements

### R1. Native ESP-IDF v5.x Toolchain & Project Environment Setup (using uv & pyenv)
- Install and configure ESP-IDF v5.3 in `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf`.
- Toolchains, virtualenv, and tools MUST be placed strictly in `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif` using `export IDF_TOOLS_PATH="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif"` to prevent filling the root filesystem `/`.
- Use Python 3.11 managed via `pyenv` (`/home/nixon/.pyenv/versions/3.11.14/bin/python3`) as the base Python interpreter.
- Leverage `uv` (`/usr/bin/uv`) for ultra-fast virtualenv creation and package installation (set `export ESP_IDF_USE_UV=1` and `export UV_CACHE_DIR="/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.cache/uv"` to prevent caching on `/`).
- Run `install.sh esp32c3`.
- Provide `source_idf.sh` in the project root to load the environment seamlessly (exporting `IDF_TOOLS_PATH`, `UV_CACHE_DIR`, and sourcing `export.sh`).
- Target chip: ESP32-C3 (`CONFIG_IDF_TARGET="esp32c3"`).
- Dual OTA partition scheme for 4MB flash defined in `partitions.csv`:
  - `nvs`: `0x9000`, size `0x5000`
  - `otadata`: `0xe000`, size `0x2000`
  - `app0`: `0x10000`, size `0x1A0000`
  - `app1`: `0x1B0000`, size `0x1A0000`

### R2. Nokia 5110 LCD Hardware SPI Driver & Motorcycle HUD Graphics
- PCD8544 LCD driver implemented using native ESP-IDF SPI Master driver (`driver/spi_master.h`) with 84x48 framebuffer (504 bytes).
- Contrast adjustment command range (`0x38` - `0x48`).
- Backlight PWM brightness control via `driver/ledc.h` on GPIO 2.
- UI rendering engine:
  - Top status bar: Aki battery voltage (e.g. `12.4V`, hidden if voltmeter disabled), ambient temperature (e.g. `29°C`, hidden if DHT disabled), BLE connection status indicator, and clock time.
  - Main area: Large speedometer digits (`km/h`) and turn-by-turn navigation turn icon.
  - Overlay support for incoming call/message notifications.

### R3. Apache NimBLE & Chronos Companion App Integration
- BLE GATT server using Apache NimBLE (`esp_nimble_hci`) configured to interface with the Android Chronos companion app.
- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`.
- Characteristics:
  - RX UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e` (Write / Write NR)
  - TX UUID: `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (Notify)
- Implements Chronos protocol packet decoding:
  - Navigation info (`0xEF`): title, duration, distance, eta, directions, GPS speed string.
  - Navigation icon (`0xEE`): 48x48 1-bit bitmap in 3 chunks of 96 bytes (`pos` 0, 1, 2).
  - Notifications (`0xAB 0x72`): caller/message alert data.
  - Time sync (`0xAB 0x93`): synchronize ESP32 system time.
- Implements Chronos handshake info & battery response upon notification subscription.

### R4. Modular Hardware Sensors & Fallback Logic
- **Aki Voltmeter (ADC) [OPTIONAL]:**
  - Circuit: Voltage Divider (10kΩ + 2.2kΩ + Zener 3.3V) connected to GPIO 0 (`ADC1_CHANNEL_0`).
  - Driver: `esp_adc/adc_oneshot.h` and `esp_adc/adc_cali.h`.
  - Configurable ratio (default `5.54`) and toggle switch.
  - If disabled via Web UI, ADC sampling stops and battery indicator is hidden from HUD status bar.
- **Speedometer Hardware Pulsa VSS (GPIO Interrupt) [OPTIONAL]:**
  - Circuit: Vario pink/green pulse wire (10kΩ + 4.7kΩ + Zener 3.3V) connected to GPIO 5.
  - Driver: `gpio_install_isr_service()` measuring pulse frequency.
  - Configurable pulse calibration (pulses/meter) and toggle switch.
  - If disabled via Web UI, speedometer calculation automatically falls back to BLE GPS speed provided by Chronos.
- **Ambient Temperature & Humidity Sensor (DHT11/DHT22) [OPTIONAL]:**
  - Circuit: Single-wire data connected to GPIO 3 with 4.7kΩ/10kΩ pull-up resistor to 3.3V.
  - Driver: Microsecond single-wire reader supporting DHT11 and DHT22.
  - Configurable toggle switch and sensor model selection (DHT11 / DHT22) stored in NVS.
  - If disabled via Web UI, reading stops, and temperature/humidity is hidden from HUD display and marked inactive.
- **LDR Ambient Light Sensor for Auto Backlight [OPTIONAL]:**
  - Circuit: LDR + 10kΩ voltage divider connected to GPIO 1 (`ADC1_CHANNEL_1`).
  - Modes: `0 = Manual OFF`, `1 = Manual ON` (with duty slider), `2 = Auto (LDR)` (backlight turns on/adjusts brightness in dark/tunnel, turns off/dims in daylight).
  - Configurable sensitivity threshold and toggle in Web UI; settings stored in NVS.

### R5. Wi-Fi SoftAP, Web Configuration Portal & Web OTA
- Wi-Fi SoftAP: SSID `VarioHUD-Setup` at `192.168.4.1`.
- Web Server using `esp_http_server`:
  - Serves responsive single-page Web Portal (HTML5 + CSS + JS) embedded directly in flash.
  - Live status display: BLE connection status, battery voltage, ambient temperature & humidity, ambient light level (ADC), real-time speed, speed source ("VSS" or "BLE GPS"), and firmware version.
  - Configuration form:
    - Voltmeter toggle & ratio multiplier.
    - VSS pulse toggle & pulses/meter calibration.
    - DHT temperature sensor toggle & sensor type (DHT11/DHT22).
    - LDR light sensor toggle & auto backlight threshold.
    - LCD contrast slider (`0x38` - `0x48`) & Backlight mode (Off / On / Auto) + manual brightness slider.
    - Saves directly to NVS without rebooting.
  - Web OTA Update form: Binary file picker (`firmware.bin`) with AJAX upload progress bar, POST to `/update`, flashes active OTA partition using `esp_ota_ops.h`, and reboots the ESP32 upon success.

## Acceptance Criteria

### Automated Build & Toolchain
- [ ] ESP-IDF v5.3 is installed on `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf` with tools in `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif`.
- [ ] Base Python interpreter is pyenv's Python 3.11, and `uv` is utilized for package/venv operations with `UV_CACHE_DIR` redirected to NVMe storage.
- [ ] Root partition `/` usage does NOT increase beyond safety limits (< 100MB consumed).
- [ ] `source_idf.sh` loads the IDF environment cleanly in bash.
- [ ] Executing `idf.py build` succeeds with return code 0, generating binary `esp32-scooter-dashboard.bin`.
- [ ] Partition table binary matches the specified 4MB dual-OTA layout (`app0` and `app1` @ 0x1A0000 each).

### Firmware Architecture & Verification
- [ ] Display driver compiles and implements SPI Master initialization and 84x48 buffer flush.
- [ ] Backlight PWM (`driver/ledc.h`) compiles with brightness control on GPIO 2.
- [ ] NimBLE Chronos service compiles and decodes navigation (`0xEF`), icon (`0xEE`), notifications (`0xAB 0x72`), and time (`0xAB 0x93`).
- [ ] ADC voltmeter module compiles with dynamic enable/disable and eFuse calibration.
- [ ] Speedometer ISR module compiles with hardware frequency calculation and BLE GPS speed fallback.
- [ ] DHT module compiles with single-wire readout on GPIO 3 with dynamic enable/disable.
- [ ] LDR light sensor module compiles with ADC readout on GPIO 1 and automatic backlight control loop.
- [ ] HTTP server compiles endpoints `GET /`, `GET /api/status`, `GET /api/config`, `POST /api/config`, and `POST /update`.
- [ ] Web OTA handler correctly interfaces with `esp_ota_begin()`, `esp_ota_write()`, `esp_ota_end()`, and `esp_ota_set_boot_partition()`.

## 2026-10-05T14:51:58Z

DIY Smart Motorcycle HUD & Telemetry Dashboard native firmware based on ESP-IDF (v5.x) for Honda Vario 125 Old LED (K60) using ESP32-C3 SuperMini and Nokia 5110 LCD (PCD8544). Includes Apache NimBLE integration with the Android Chronos companion app, modular optional hardware sensors (Aki Voltmeter ADC, VSS Speedometer GPIO ISR, DHT11/DHT22 Ambient Temperature Sensor, and LDR Light Sensor for Auto Backlight) with automatic BLE GPS speed fallback, and an embedded single-page Web Configuration & Web OTA update portal.

Working directory: `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard`
Integrity mode: development

## Resumption & Existing State Notice
- **Milestone 1 is ALREADY COMPLETE and PASSED**:
  - ESP-IDF v5.3 is installed and verified on NVMe at `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf`.
  - Tools and caches are isolated in `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif`.
  - Environment can be sourced immediately via `. ./source_idf.sh`.
  - Dual-OTA partition table `partitions.csv`, `sdkconfig.defaults`, and project skeleton are in place and verified (`idf.py build` compiles rc=0).
  - Test harness with 197 test cases is published in `tests/e2e/` and `TEST_READY.md`.
  - Full orchestrator plan and decomposition exist in `.agents/orchestrator/PROJECT.md` and `.agents/orchestrator/progress.md`.
- **Action**: Resume execution from **Milestone 2 (Display PCD8544, UI Engine & Backlight PWM Driver)** through **Milestone 6 (System Integration & Final Acceptance)** without re-running Milestone 1 toolchain setup.

Reference documentation:
- Chronos ESP32 BLE API & Protocol: https://github.com/fbiego/chronos-esp32/blob/main/api.md

## Complete Pin Assignment (ESP32-C3 SuperMini)
- **GPIO 0 (ADC1_CH0):** Aki Voltmeter divider (10kΩ + 2.2kΩ + 3.3V Zener) [OPTIONAL]
- **GPIO 1 (ADC1_CH1):** LDR Light Sensor divider (LDR + 10kΩ to GND) for Auto Backlight [OPTIONAL]
- **GPIO 2 (LEDC PWM):** Nokia 5110 Backlight control (PWM brightness dimming)
- **GPIO 3 (GPIO):** DHT11/DHT22 Ambient Temperature & Humidity Sensor (1-wire) [OPTIONAL]
- **GPIO 4 (GPIO):** Nokia 5110 DC (Data / Command)
- **GPIO 5 (GPIO ISR):** Speedometer VSS pulse interrupt (Vario pink/green wire) [OPTIONAL]
- **GPIO 6 (SPI SCK):** Nokia 5110 SCK / SCLK
- **GPIO 7 (SPI MOSI):** Nokia 5110 MOSI / DIN
- **GPIO 8 (GPIO):** Nokia 5110 CS / CE (Chip Select)
- **GPIO 10 (GPIO):** Nokia 5110 RST (Reset)
- **GPIO 20 / 21:** Dedicated USB/UART serial monitor and programming

## Requirements

### R1. Native ESP-IDF v5.x Toolchain & Project Environment Setup (ALREADY COMPLETED in M1)
- Verified working environment: `. ./source_idf.sh`
- Target chip: ESP32-C3 (`CONFIG_IDF_TARGET="esp32c3"`).
- Dual OTA partition scheme for 4MB flash defined in `partitions.csv`:
  - `nvs`: `0x9000`, size `0x5000`
  - `otadata`: `0xe000`, size `0x2000`
  - `app0`: `0x10000`, size `0x1A0000`
  - `app1`: `0x1B0000`, size `0x1A0000`

### R2. Nokia 5110 LCD Hardware SPI Driver & Motorcycle HUD Graphics (Milestone 2)
- PCD8544 LCD driver implemented using native ESP-IDF SPI Master driver (`driver/spi_master.h`) with 84x48 framebuffer (504 bytes).
- Contrast adjustment command range (`0x38` - `0x48`).
- Backlight PWM brightness control via `driver/ledc.h` on GPIO 2.
- UI rendering engine:
  - Top status bar: Aki battery voltage (e.g. `12.4V`, hidden if voltmeter disabled), ambient temperature (e.g. `29°C`, hidden if DHT disabled), BLE connection status indicator, and clock time.
  - Main area: Large speedometer digits (`km/h`) and turn-by-turn navigation turn icon.
  - Overlay support for incoming call/message notifications.

### R3. Apache NimBLE & Chronos Companion App Integration (Milestone 3)
- BLE GATT server using Apache NimBLE (`esp_nimble_hci`) configured to interface with the Android Chronos companion app.
- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`.
- Characteristics:
  - RX UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e` (Write / Write NR)
  - TX UUID: `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (Notify)
- Implements Chronos protocol packet decoding:
  - Navigation info (`0xEF`): title, duration, distance, eta, directions, GPS speed string.
  - Navigation icon (`0xEE`): 48x48 1-bit bitmap in 3 chunks of 96 bytes (`pos` 0, 1, 2).
  - Notifications (`0xAB 0x72`): caller/message alert data.
  - Time sync (`0xAB 0x93`): synchronize ESP32 system time.
- Implements Chronos handshake info & battery response upon notification subscription.

### R4. Modular Hardware Sensors & Fallback Logic (Milestone 4)
- **Aki Voltmeter (ADC) [OPTIONAL]:**
  - Circuit: Voltage Divider (10kΩ + 2.2kΩ + Zener 3.3V) connected to GPIO 0 (`ADC1_CHANNEL_0`).
  - Driver: `esp_adc/adc_oneshot.h` and `esp_adc/adc_cali.h`.
  - Configurable ratio (default `5.54`) and toggle switch.
  - If disabled via Web UI, ADC sampling stops and battery indicator is hidden from HUD status bar.
- **Speedometer Hardware Pulsa VSS (GPIO Interrupt) [OPTIONAL]:**
  - Circuit: Vario pink/green pulse wire (10kΩ + 4.7kΩ + Zener 3.3V) connected to GPIO 5.
  - Driver: `gpio_install_isr_service()` measuring pulse frequency.
  - Configurable pulse calibration (pulses/meter) and toggle switch.
  - If disabled via Web UI, speedometer calculation automatically falls back to BLE GPS speed provided by Chronos.
- **Ambient Temperature & Humidity Sensor (DHT11/DHT22) [OPTIONAL]:**
  - Circuit: Single-wire data connected to GPIO 3 with 4.7kΩ/10kΩ pull-up resistor to 3.3V.
  - Driver: Microsecond single-wire reader supporting DHT11 and DHT22.
  - Configurable toggle switch and sensor model selection (DHT11 / DHT22) stored in NVS.
  - If disabled via Web UI, reading stops, and temperature/humidity is hidden from HUD display and marked inactive.
- **LDR Ambient Light Sensor for Auto Backlight [OPTIONAL]:**
  - Circuit: LDR + 10kΩ voltage divider connected to GPIO 1 (`ADC1_CHANNEL_1`).
  - Modes: `0 = Manual OFF`, `1 = Manual ON` (with duty slider), `2 = Auto (LDR)` (backlight turns on/adjusts brightness in dark/tunnel, turns off/dims in daylight).
  - Configurable sensitivity threshold and toggle in Web UI; settings stored in NVS.

### R5. Wi-Fi SoftAP, Web Configuration Portal & Web OTA (Milestone 5)
- Wi-Fi SoftAP: SSID `VarioHUD-Setup` at `192.168.4.1`.
- Web Server using `esp_http_server`:
  - Serves responsive single-page Web Portal (HTML5 + CSS + JS) embedded directly in flash.
  - Live status display: BLE connection status, battery voltage, ambient temperature & humidity, ambient light level (ADC), real-time speed, speed source ("VSS" or "BLE GPS"), and firmware version.
  - Configuration form:
    - Voltmeter toggle & ratio multiplier.
    - VSS pulse toggle & pulses/meter calibration.
    - DHT temperature sensor toggle & sensor type (DHT11/DHT22).
    - LDR light sensor toggle & auto backlight threshold.
    - LCD contrast slider (`0x38` - `0x48`) & Backlight mode (Off / On / Auto) + manual brightness slider.
    - Saves directly to NVS without rebooting.
  - Web OTA Update form: Binary file picker (`firmware.bin`) with AJAX upload progress bar, POST to `/update`, flashes active OTA partition using `esp_ota_ops.h`, and reboots the ESP32 upon success.

## Acceptance Criteria

### Automated Build & Toolchain
- [x] ESP-IDF v5.3 installed on `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf` with tools in `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif`.
- [x] Root partition `/` usage protected (0 MB increase).
- [x] `source_idf.sh` loads the IDF environment cleanly in bash.
- [ ] Executing `idf.py build` succeeds with return code 0 with all modules integrated, generating binary `esp32-scooter-dashboard.bin`.
- [ ] Partition table binary matches the specified 4MB dual-OTA layout (`app0` and `app1` @ 0x1A0000 each).

### Firmware Architecture & Verification
- [ ] Display driver compiles and implements SPI Master initialization and 84x48 buffer flush.
- [ ] Backlight PWM (`driver/ledc.h`) compiles with brightness control on GPIO 2.
- [ ] NimBLE Chronos service compiles and decodes navigation (`0xEF`), icon (`0xEE`), notifications (`0xAB 0x72`), and time (`0xAB 0x93`).
- [ ] ADC voltmeter module compiles with dynamic enable/disable and eFuse calibration.
- [ ] Speedometer ISR module compiles with hardware frequency calculation and BLE GPS speed fallback.
- [ ] DHT module compiles with single-wire readout on GPIO 3 with dynamic enable/disable.
- [ ] LDR light sensor module compiles with ADC readout on GPIO 1 and automatic backlight control loop.
- [ ] HTTP server compiles endpoints `GET /`, `GET /api/status`, `GET /api/config`, `POST /api/config`, and `POST /update`.
- [ ] Web OTA handler correctly interfaces with `esp_ota_begin()`, `esp_ota_write()`, `esp_ota_end()`, and `esp_ota_set_boot_partition()`.

## 2026-10-05T15:22:53Z

DIY Smart Motorcycle HUD & Telemetry Dashboard native firmware based on ESP-IDF (v5.x) for Honda Vario 125 Old LED (K60) using ESP32-C3 SuperMini and Nokia 5110 LCD (PCD8544). Includes Apache NimBLE integration with the Android Chronos companion app, modular optional hardware sensors (Aki Voltmeter ADC, VSS Speedometer GPIO ISR, DHT11/DHT22 Ambient Temperature Sensor, and LDR Light Sensor for Auto Backlight) with automatic BLE GPS speed fallback, and an embedded single-page Web Configuration & Web OTA update portal.

Working directory: `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard`
Integrity mode: development

## Resumption & Current Working State
- **Milestone 1 is COMPLETE & VERIFIED**:
  - ESP-IDF v5.3 installed on NVMe (`/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf`), tools in `.espressif`.
  - Partition table `partitions.csv` (dual 1.6MB OTA slots for 4MB flash).
  - Environment loader `. ./source_idf.sh` tested and fully functional.
- **Milestone 2 is COMPLETE & VERIFIED**:
  - `main/include/pcd8544.h`, `main/display/pcd8544.c`: Hardware SPI master driver for Nokia 5110 with contrast adjustment.
  - `main/include/hud_ui.h`, `main/display/hud_ui.c`, `main/display/font5x7.h`: HUD layout engine with big speed digits, turn icon rendering, and status bar.
  - LEDC PWM backlight control on GPIO 2.
  - `main/include/hud_state.h`, `main/hud_state.c`: Thread-safe telemetry state management.
  - Build verified: `idf.py build` succeeds with return code 0, generating `esp32-scooter-dashboard.bin` (209 KB, 87% partition free).
- **ACTION FOR NEW SWARM**: Resume execution from **Milestone 3 (NimBLE Chronos GATT Server)** $\rightarrow$ **Milestone 4 (Modular Hardware Sensors)** $\rightarrow$ **Milestone 5 (Wi-Fi SoftAP, Web Portal & OTA)** $\rightarrow$ **Milestone 6 (Final System Integration)**.

Reference documentation:
- Chronos ESP32 BLE API & Protocol: https://github.com/fbiego/chronos-esp32/blob/main/api.md

## Complete Pin Assignment (ESP32-C3 SuperMini)
- **GPIO 0 (ADC1_CH0):** Aki Voltmeter divider (10kΩ + 2.2kΩ + 3.3V Zener) [OPTIONAL]
- **GPIO 1 (ADC1_CH1):** LDR Light Sensor divider (LDR + 10kΩ to GND) for Auto Backlight [OPTIONAL]
- **GPIO 2 (LEDC PWM):** Nokia 5110 Backlight control (PWM brightness dimming)
- **GPIO 3 (GPIO):** DHT11/DHT22 Ambient Temperature & Humidity Sensor (1-wire) [OPTIONAL]
- **GPIO 4 (GPIO):** Nokia 5110 DC (Data / Command)
- **GPIO 5 (GPIO ISR):** Speedometer VSS pulse interrupt (Vario pink/green wire) [OPTIONAL]
- **GPIO 6 (SPI SCK):** Nokia 5110 SCK / SCLK
- **GPIO 7 (SPI MOSI):** Nokia 5110 MOSI / DIN
- **GPIO 8 (GPIO):** Nokia 5110 CS / CE (Chip Select)
- **GPIO 10 (GPIO):** Nokia 5110 RST (Reset)
- **GPIO 20 / 21:** Dedicated USB/UART serial monitor and programming

## Requirements

### R1. Native ESP-IDF v5.x Toolchain & Project Environment Setup (DONE in M1)
- Verified environment loader: `. ./source_idf.sh`
- Target chip: ESP32-C3 (`CONFIG_IDF_TARGET="esp32c3"`).
- Dual OTA partition scheme for 4MB flash defined in `partitions.csv`.

### R2. Nokia 5110 LCD Hardware SPI Driver & Motorcycle HUD Graphics (DONE in M2)
- PCD8544 LCD driver implemented using native ESP-IDF SPI Master driver (`driver/spi_master.h`) with 84x48 framebuffer (504 bytes).
- Contrast adjustment command range (`0x38` - `0x48`).
- Backlight PWM brightness control via `driver/ledc.h` on GPIO 2.
- UI rendering engine:
  - Top status bar: Aki battery voltage, ambient temperature, BLE status indicator, and clock time.
  - Main area: Large speedometer digits (`km/h`) and turn-by-turn navigation turn icon.
  - Overlay support for incoming call/message notifications.

### R3. Apache NimBLE & Chronos Companion App Integration (Milestone 3)
- BLE GATT server using Apache NimBLE (`esp_nimble_hci`) configured to interface with the Android Chronos companion app.
- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`.
- Characteristics:
  - RX UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e` (Write / Write NR)
  - TX UUID: `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (Notify)
- Implements Chronos protocol packet decoding:
  - Navigation info (`0xEF`): title, duration, distance, eta, directions, GPS speed string.
  - Navigation icon (`0xEE`): 48x48 1-bit bitmap in 3 chunks of 96 bytes (`pos` 0, 1, 2).
  - Notifications (`0xAB 0x72`): caller/message alert data.
  - Time sync (`0xAB 0x93`): synchronize ESP32 system time.
- Implements Chronos handshake info & battery response upon notification subscription.

### R4. Modular Hardware Sensors & Fallback Logic (Milestone 4)
- **Aki Voltmeter (ADC) [OPTIONAL]:**
  - Circuit: Voltage Divider (10kΩ + 2.2kΩ + Zener 3.3V) connected to GPIO 0 (`ADC1_CHANNEL_0`).
  - Driver: `esp_adc/adc_oneshot.h` and `esp_adc/adc_cali.h`.
  - Configurable ratio (default `5.54`) and toggle switch.
  - If disabled via Web UI, ADC sampling stops and battery indicator is hidden from HUD status bar.
- **Speedometer Hardware Pulsa VSS (GPIO Interrupt) [OPTIONAL]:**
  - Circuit: Vario pink/green pulse wire (10kΩ + 4.7kΩ + Zener 3.3V) connected to GPIO 5.
  - Driver: `gpio_install_isr_service()` measuring pulse frequency.
  - Configurable pulse calibration (pulses/meter) and toggle switch.
  - If disabled via Web UI, speedometer calculation automatically falls back to BLE GPS speed provided by Chronos.
- **Ambient Temperature & Humidity Sensor (DHT11/DHT22) [OPTIONAL]:**
  - Circuit: Single-wire data connected to GPIO 3 with 4.7kΩ/10kΩ pull-up resistor to 3.3V.
  - Driver: Microsecond single-wire reader supporting DHT11 and DHT22.
  - Configurable toggle switch and sensor model selection (DHT11 / DHT22) stored in NVS.
  - If disabled via Web UI, reading stops, and temperature/humidity is hidden from HUD display and marked inactive.
- **LDR Ambient Light Sensor for Auto Backlight [OPTIONAL]:**
  - Circuit: LDR + 10kΩ voltage divider connected to GPIO 1 (`ADC1_CHANNEL_1`).
  - Modes: `0 = Manual OFF`, `1 = Manual ON` (with duty slider), `2 = Auto (LDR)` (backlight turns on/adjusts brightness in dark/tunnel, turns off/dims in daylight).
  - Configurable sensitivity threshold and toggle in Web UI; settings stored in NVS.

### R5. Wi-Fi SoftAP, Web Configuration Portal & Web OTA (Milestone 5)
- Wi-Fi SoftAP: SSID `VarioHUD-Setup` at `192.168.4.1`.
- Web Server using `esp_http_server`:
  - Serves responsive single-page Web Portal (HTML5 + CSS + JS) embedded directly in flash.
  - Live status display: BLE connection status, battery voltage, ambient temperature & humidity, ambient light level (ADC), real-time speed, speed source ("VSS" or "BLE GPS"), and firmware version.
  - Configuration form:
    - Voltmeter toggle & ratio multiplier.
    - VSS pulse toggle & pulses/meter calibration.
    - DHT temperature sensor toggle & sensor type (DHT11/DHT22).
    - LDR light sensor toggle & auto backlight threshold.
    - LCD contrast slider (`0x38` - `0x48`) & Backlight mode (Off / On / Auto) + manual brightness slider.
    - Saves directly to NVS without rebooting.
  - Web OTA Update form: Binary file picker (`firmware.bin`) with AJAX upload progress bar, POST to `/update`, flashes active OTA partition using `esp_ota_ops.h`, and reboots the ESP32 upon success.

## Acceptance Criteria

### Automated Build & Toolchain
- [x] ESP-IDF v5.3 installed on `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/esp-idf` with tools in `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif`.
- [x] Root partition `/` usage protected (0 MB increase).
- [x] `source_idf.sh` loads the IDF environment cleanly in bash.
- [x] Display driver compiles and implements SPI Master initialization and 84x48 buffer flush.
- [x] Backlight PWM (`driver/ledc.h`) compiles with brightness control on GPIO 2.
- [ ] NimBLE Chronos service compiles and decodes navigation (`0xEF`), icon (`0xEE`), notifications (`0xAB 0x72`), and time (`0xAB 0x93`).
- [ ] ADC voltmeter module compiles with dynamic enable/disable and eFuse calibration.
- [ ] Speedometer ISR module compiles with hardware frequency calculation and BLE GPS speed fallback.
- [ ] DHT module compiles with single-wire readout on GPIO 3 with dynamic enable/disable.
- [ ] LDR light sensor module compiles with ADC readout on GPIO 1 and automatic backlight control loop.
- [ ] HTTP server compiles endpoints `GET /`, `GET /api/status`, `GET /api/config`, `POST /api/config`, and `POST /update`.
- [ ] Web OTA handler correctly interfaces with `esp_ota_begin()`, `esp_ota_write()`, `esp_ota_end()`, and `esp_ota_set_boot_partition()`.
- [ ] Executing `idf.py build` succeeds with return code 0 with all modules integrated, generating binary `esp32-scooter-dashboard.bin`.
- [ ] Partition table binary matches the specified 4MB dual-OTA layout (`app0` and `app1` @ 0x1A0000 each).
## 2026-10-06T10:25:12Z — Visual CAD Audit & Solid Geometry Correction

# Teamwork Project — Visual CAD Audit & Solid Geometry Correction

Audit the 3D models visually and geometrically, correct physical CAD defects in `cad/generate_cad.py` that were hidden by translucent wireframe rendering, verify zero-interference assembly, and produce high-resolution, solid-shaded (opaque diffuse-lit, non-wireframe) 3D renders.

Working directory: `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard/cad`
Integrity mode: development

## Discovered Physical Defects to Fix

### 1. Hinge Pivot Interference (94.09 mm³ Collision)
- **Problem:** `mirror_bracket.stl` arm hull expands to 16.0mm width near the pivot tongue, while `main_housing.stl` clevis gap is only 12.2mm. The arm shoulders violently collide with the housing clevis ears when assembled.
- **Fix:** Confine the tongue and neck transition strictly to <= 11.2mm width for at least 15mm radially around the pivot axis so the housing clevis can tilt freely (+/- 25 deg) without rubbing or jamming against the bracket arm.

### 2. Main Housing M4 Nut Pockets Buried (0.4mm Skin)
- **Problem:** `nut` pocket was translated with 0.4mm Z-offset, leaving a 0.4mm membrane over the hexagonal pocket on the rear face instead of breaking through flush for nut insertion.
- **Fix:** Extend the hexagonal cylinder cut to start at Z = -1.0mm to ensure 100% clean, open entry on the rear face with zero bridging skin.

### 3. Solid Shaded Visual Renders (Eliminate Wireframes)
- **Problem:** Previous renders used transparent faces with black edge lines (alpha=0.3, edgecolor='k'), creating an unintelligible wireframe mesh where solid surfaces, wall thicknesses, and collisions could not be seen.
- **Requirement:** Generate clean, opaque, Lambertian diffuse-shaded renders with directional lighting, solid material colors, and edgecolor='none'. Produce both individual part views and assembled cross-sections.

## Acceptance Criteria
- [ ] `mirror_bracket ^ main_housing` boolean intersection volume equals 0.0 mm³ across tilt range [-25 deg, +25 deg].
- [ ] Housing M4 nut pockets cleanly penetrate the rear surface with zero membrane obstruction.
- [ ] All 4 STLs re-exported and verified watertight (`is_watertight == True`) and single solid component (`len(decompose()) == 1`).
- [ ] High-resolution solid-shaded render generated with zero wireframe artifacts and saved to artifacts.

## 2026-10-06T20:33:30Z — 3D CAD Refinement & Mechanical Feature Integration

# Teamwork Project — 3D CAD Refinement & Mechanical Feature Integration

Refine and upgrade the scooter dashboard 3D CAD model (`cad/generate_cad.py`) to address three critical mechanical issues identified during slicer inspection: eliminate the outer corner double-chamfer ridge on the housing, replace the dangling bottom clip with a rigid rear interlocking V-rail (dovetail) sensor mount, and add radial indexing serrations inside the hinge pivot to lock screen tilt against vibration.

Working directory: `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard/cad`
Integrity mode: development

---

## Requirements

### R1. Elimination of Outer Double-Chamfer Ridge on Main Housing
- The outer perimeter and corners of `main_housing.stl` must have a clean, continuous single-radius rounded profile with zero stepped ridges, secondary fillets, or double chamfers.
- Internal corner screw standoff pillars must remain strictly within the housing interior boundary with 0.0000 mm³ protrusion beyond the outer shell profile.
- All 4 M4 corner nut pockets must remain cleanly recessed in the rear face with open access and zero membrane obstruction.

### R2. Rear Interlocking V-Rail (Dovetail) Sensor Mount & Clean Bottom Face
- **Bottom Face Cleanup:** Remove the dangling bottom clip dock from `main_housing.stl`. The bottom face must only contain the cylindrical cable gland collar (dia 6.5 mm ID, dia 12.0 mm OD).
- **Male V-Rail on Rear Housing Face:** Integrate an interlocking male V-rail (dovetail) with 45° undercut wings on the lower rear face of `main_housing.stl`:
  - Narrow root at housing wall, flared crown protruding backward to prevent lateral and pull-out movement.
  - Positive mechanical end-stop preventing over-travel.
  - Tactile snap-detent bump (0.35 - 0.40 mm) to positively lock against road vibration.
  - Positioned in the collision-free lower rear zone (Y in [-25.0, -11.0] mm, X in [-8.0, +8.0] mm) with zero interference with the mirror bracket tilt arm across [-25 deg, +25 deg].
- **Mating Female Sensor Pod (`dht11_mount.stl`):** Redesign the sensor pod with a mating female dovetail channel:
  - 0.25 mm bilateral sliding tolerance for snug FDM PETG fit.
  - Internal bay for DHT11 sensor (15.5 x 29.0 x 11.5 mm).
  - Downward rain-deflecting louvers and bottom drainage.
  - Bottom wire exit aligned toward the adjacent housing cable gland.

### R3. Radial Indexing Serrations (Gerigi Engsel) on Hinge Pivot
- Add radial rosette teeth / indexing serrations to the mating hinge pivot faces:
  - Inner face of the housing clevis ear(s) and matching outer flank of the mirror bracket tongue.
  - Radial span R approx 3.5 - 9.5 mm concentric with the M4 hinge pivot hole.
  - Uniform angular pitch (e.g., 24 teeth at 15 deg or 36 teeth at 10 deg) with tooth height h approx 0.6 - 0.75 mm and 60 - 90 deg triangular tooth profile.
  - When the M4 clamping bolt is tightened, the teeth interlock positively to resist handlebar vibration and allow incremental angle adjustment.

### R4. Automated Geometric Verification & Solid Shaded Renders
- Code execution strictly managed via `uv run` (protecting host Arch Linux system).
- All 4 generated STLs must be 100% watertight single manifold solids (`is_watertight == True`, `len(decompose()) == 1`).
- Mirror bracket arm must have 0.0000 mm³ collision with housing across full tilt range [-25 deg, +25 deg].
- Generate high-resolution solid-shaded (opaque diffuse-lit, `edgecolor='none'`) verification renders displaying:
  1. Outer housing corners showing the eliminated double chamfer.
  2. Rear V-rail and sliding sensor pod assembly.
  3. Close-up cross-section of the radial hinge indexing teeth.
  4. Complete 4-part exploded and assembled views.

---

## Acceptance Criteria

### Geometric Integrity & Fit
- [ ] Standoff excess protrusion volume outside outer housing body is strictly 0.0000 mm³.
- [ ] Bottom wall of `main_housing` is completely clean except for the cable gland collar.
- [ ] Rear male V-rail has 45° undercut wings, positive mechanical end-stop, and snap detent.
- [ ] `dht11_mount.stl` mates cleanly via female dovetail channel with 0.25 mm clearance.
- [ ] Radial indexing teeth on clevis ear and tongue mate cleanly at discrete angle steps (15 deg or 10 deg).
- [ ] Zero collision between bracket arm and housing across tilt range [-25 deg, +25 deg].
- [ ] All 4 STLs verified watertight single solids with valid bounding boxes.

### Visual Verification
- [ ] High-resolution solid-shaded renders produced and verified in artifacts.
- [ ] No wireframe artifacts, missing faces, or non-manifold edges.


## 2026-10-07T07:23:41Z

# Teamwork Project — Comprehensive 3D CAD & Mechanical Design Review

Perform an exhaustive mechanical engineering and Design for Additive Manufacturing (DFAM) audit on the scooter dashboard CAD model (`cad/generate_cad.py` and exported STLs: `front_bezel.stl`, `main_housing.stl`, `mirror_bracket.stl`, `dht11_mount.stl`). Specifically audit the internal component layout and mounting trays (tatakan komponen), conduct thorough visual inspection using high-resolution solid-shaded cutaways, and formulate actionable, high-impact improvement proposals.

Working directory: `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard/cad`
Integrity mode: development

---

## Requirements

### R1. Manufacturing & Printability Audit (Bambu Lab A1 Mini / PETG)
- Audit all 4 parts for FDM printability:
  - Optimal print build plate orientation to minimize or eliminate necessary supports.
  - Identification of unsupported overhangs (> 45°) or critical bridging spans.
  - Layer-line orientation vs principal stress axes (evaluating delamination risk on clevis ears, mirror stalk, and dovetail wings under motorcycle vibration).

### R2. Weatherproofing & Environmental Sealing Audit (IP54+ Real-World Scooter Use)
- Evaluate real-world water exclusion under heavy tropical rain and road spray:
  - Bezel-to-housing labyrinth tongue-and-groove joint (tolerance, sealing efficacy, compatibility with 1.0mm silicone cord or sealant).
  - Screen window acrylic recessed pocket (water pooling risks, sealant channel).
  - Cable gland collar entry (strain relief effectiveness, drip loop geometry).
  - Rear V-rail sensor pod drainage and humidity dissipation under direct sunlight.

### R3. Internal Component Layout & Mounting Trays Audit (Tata Letak Komponen & Tatakan)
- **Nokia 5110 LCD Shelf & Retention:**
  - Audit continuous 3-sided shelf ($Z = 16.5$ mm): does it positively prevent PCB shifting/rattling under road bumps? Is there clamping pressure or a foam gasket contact from the bezel?
  - Pin header clearance: check clearance for the 8-pin header/solder pads against the housing wall and ensure no short-circuit risk.
- **ESP32-C3 SuperMini Bay & Retention:**
  - Dimensional fit of tray ($22.0 \times 24.0$ mm perimeter) vs actual board dimensions ($22.5 \times 18.0$ mm).
  - Retention mechanism: evaluate snap tabs, friction ribs, or screw standoffs so the microcontroller cannot dislodge during motorcycle vibration.
  - Antenna RF clearance: verify that the ceramic/PCB antenna is not shielded by wiring or metal standoffs.
  - USB-C port orientation and firmware flashing/maintenance access.
- **MP1584EN Step-Down Module Bay & Thermal Management:**
  - Dimensional fit and retention for MP1584EN buck converter ($22.0 \times 17.0$ mm).
  - Standoff / air gap beneath the module to prevent direct thermal conduction to the PETG floor under continuous 12V→3.3V load.
  - Trimpot adjustment tool clearance.
- **Internal Wire Routing Channels & Strain Relief:**
  - Wire passage through or over the center isolation barrier between buck converter and ESP32.
  - Wire path from the bottom cable gland past the zip-tie anchor bridge to both trays and up to the LCD.
  - Clearance between internal components and the closing bezel tongue to eliminate any wire pinch hazards.

### R4. Structural Rigidity, Vibration Resistance & Fasteners Audit
- Evaluate structural integrity under continuous engine/road vibration:
  - M4 fastener retention (hex pocket depth for standard vs nyloc lock nuts, washer seating).
  - Mirror bracket stalk stiffness and stress concentration at the bend/fillet transitions under wind drag and bumps.
  - Hinge radial rosette teeth engagement under clamp load (tooth shear strength and plastic deformation risk in PETG).

### R5. Usability, Ergonomics & Assembly Experience Audit
- Review practical day-to-day usability and assembly workflow:
  - Sunlight readability (glare angles, feasibility of an integrated sun hood / visor brow on the front bezel).
  - Practical assembly and soldering sequence (can components be installed and serviced comfortably?).
  - Ease of sliding the rear DHT11 pod on/off when the unit is mounted on the scooter handlebar.

### R6. Visual Inspection & Solid-Shaded Cutaway Renders
- Conduct a rigorous visual inspection of all components and assemblies:
  - Generate multi-angle, solid-shaded (opaque diffuse-lit, `edgecolor='none'`) renders.
  - Produce an interior perspective / bird's-eye cutaway render showcasing the internal cavity and component layout (Nokia 5110 shelf, ESP32 tray, MP1584EN bay, center barrier, zip-tie bridge).
  - Produce assembly cross-section views (XZ and YZ planes) revealing wall thicknesses, labyrinth joint engagement, screw thread depths, and clearance gaps.
  - Visually inspect for aesthetic proportions, smooth transition fillets, and absence of visual artifacts.

---

## Acceptance Criteria

### Technical Depth & Objectivity
- [ ] Comprehensive audit report generated covering R1 through R6 with quantitative measurements (wall thicknesses, angles, clearances, tray dimensions, tolerances).
- [ ] Dedicated section for **Tata Letak Komponen & Tatakan (R3)** detailing exact fit, retention method, wiring channels, and thermal separation.
- [ ] Dedicated section for **Inspeksi Visual & Render (R6)** evaluating aesthetics, wall thickness consistency, and internal packaging clearances.
- [ ] High-resolution solid-shaded render deliverables generated and verified:
  1. Interior cavity & component tray cutaway view.
  2. Multi-plane assembly cross-sections (XZ and YZ).
  3. Mated 4-part assembly with rear dovetail and hinge detail.
- [ ] Each identified issue categorized with Severity (Critical / Moderate / Minor) and actionable CAD remediation proposal with concrete parameter suggestions.
- [ ] Print orientation matrix provided for all 4 parts detailing recommended face on build plate and required supports (if any).
- [ ] Independent verification of claims against actual mesh geometry in `generate_cad.py`.
