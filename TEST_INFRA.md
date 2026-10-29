# Test Infrastructure Specification: ESP32 Scooter Dashboard

**Project:** Smart Motorcycle HUD & Telemetry Dashboard (Honda Vario 125 K60)  
**Target Hardware:** ESP32-C3 SuperMini (RISC-V 32-bit single-core @ 160MHz, 4MB Flash)  
**Framework:** ESP-IDF v5.3 (Native C / FreeRTOS)  
**Test Harness Location:** `tests/e2e/`  
**Master Test Runner:** `tests/e2e/run_e2e_tests.sh`  

---

## 1. Test Philosophy & Design Principles

The ESP32 Scooter Dashboard firmware undergoes rigorous End-to-End (E2E) verification adhering to four foundational principles:

1. **Opaque-Box Verification & Independence**:
   Tests treat the firmware application as an opaque system interacting with external electrical signals, Bluetooth LE companion devices, and Wi-Fi HTTP clients. Test cases do not rely on internal implementation hacks; instead, they validate observable system states, protocol frames, memory geometries, mathematical formulas, and interface contracts.
2. **Progressive Testability**:
   Tests are architected to support progressive milestone execution (Milestones M1 through M6). Early milestone suites (e.g. M1 Toolchain and Partitions) execute and validate early foundation artifacts, while subsequent protocol, sensor, and web portal tests verify functional logic independently using authoritative protocol and mathematical reference models.
3. **Storage & Environment Isolation**:
   Tests rigorously enforce the user's storage safety constraint: root partition `/` must never be polluted, and toolchains/virtualenvs/caches must strictly reside on NVMe storage (`/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db`).
4. **Deterministic Authoritative Oracles**:
   Expected values are mathematically derived from primary specifications:
   - Chronos BLE protocol specification (`fbiego/chronos-esp32` and `fbiego/chronos-core`).
   - Philips PCD8544 84x48 LCD controller datasheet.
   - Honda Vario 125 VSS pulse calibration mathematics.
   - ESP-IDF v5.3 dual-OTA partition specifications and binary headers.

---

## 2. 4-Tier Test Architecture

The test suite is structured into a systematic 4-Tier hierarchy:

```
+-------------------------------------------------------------------+
|               Tier 4: Real-World Application Scenarios            |
|       (Complete Trips, Tunnel Auto-Light, Mid-Ride Reconnect)     |
+-------------------------------------------------------------------+
|            Tier 3: Cross-Feature Combinatorial Interactions       |
|    (VSS Failure GPS Fallback, BLE+OTA Coex, Call Overlay vs Nav)  |
+-------------------------------------------------------------------+
|                Tier 2: Boundary & Corner Cases                    |
|      (Contrast 0x38-0x48, Duty 0-255, ADC Deadband, 1.625MB OTA)  |
+-------------------------------------------------------------------+
|                 Tier 1: Feature Coverage (F01 - F24)              |
|        (>= 5 Test Cases per Feature across Subsystem Suites)      |
+-------------------------------------------------------------------+
```

### Tier 1: Subsystem Feature Coverage (`F01` - `F24`)
Validates baseline contract conformance for all 24 features identified in `PROJECT.md`:
- **Toolchain & Partitions (`F01` - `F04`)**: Path redirections, storage protection, `source_idf.sh` syntax, 4MB dual-OTA layout, CMake configuration.
- **Display & UI (`F05` - `F09`)**: PCD8544 SPI driver, LEDC PWM backlight, top status bar, speedometer and turn icon blitting, modal notification overlay.
- **Chronos BLE Protocol (`F10` - `F15`)**: NimBLE GATT server, handshake & battery packets, 0xEF navigation info, 0xEE 3-chunk icon assembly, 0x72 notifications, 0x93 time sync.
- **Sensors & Fallback (`F16` - `F19`)**: Aki voltmeter ADC, VSS speedometer pulse frequency, DHT11/22 1-wire driver, LDR ambient light sensor and hysteresis loop.
- **Web Portal & OTA (`F20` - `F23`)**: Wi-Fi SoftAP, embedded SPA, REST APIs (`/api/status`, `/api/config`), Web OTA update stream (`/update`).
- **System Integration (`F24`)**: FreeRTOS task priority hierarchy, shared state mutex snapshot safety, 1.625MB partition budget.

### Tier 2: Boundary & Corner Cases
Stresses all boundary conditions and limits:
- Contrast bounds: `0x38`, `0x48`, below min (`0x37`), above max (`0x49`).
- Backlight duty: `0`, `255`, clamp guards.
- Voltage thresholds: `0.0V` disconnected, `10.5V` critical low, `14.8V` charging high, `24.0V` spike.
- Speedometer frequency: `0` pulses/s, `1` pulse/s, `1575.4` pulses/s (199 km/h top speed), zero-division protection.
- Climate sensors: `-20.0°C` sub-zero, `0.0°C`, `80.0°C`, checksum 8-bit wrap-around.
- LDR ADC: `0` (pitch black), `4095` (blinding sun), exact hysteresis deadband boundaries (`threshold ± hysteresis`).
- Protocol framing: zero-length strings, long strings (>150 chars), UTF-8 characters, colon index 29 vs 30 boundary.
- OTA streaming: `0` bytes rejected, `0x1A0000` exact max accepted, `0x1A0001` rejected.

### Tier 3: Cross-Feature Interactions & Combinations
Exercises pairwise and multi-peripheral concurrency:
- VSS physical pulse failure triggers seamless fallback to BLE GPS speed during active navigation.
- Web OTA binary upload stream executed during active BLE GATT connection (verifying 2.4 GHz RF coexistence).
- LDR ambient light changes trigger auto-backlight while user is reconfiguring LCD contrast via Web UI.
- Incoming phone call alert overlays screen while turn maneuver icon is updating; call dismissal cleanly restores turn icon.
- `POST /api/config` updating calibration factors while background sensor loop is sampling ADC.
- Time sync packet arrival while digital clock is rendering in top status bar.
- Disabling voltmeter or DHT sensor via Web UI instantly suppresses their visual indicators from the status bar.

### Tier 4: Real-World Operational Scenarios
Full mission simulation tests:
1. `test_s01_complete_scooter_trip_cycle`: Cold boot, BLE connect, handshake, acceleration, turn navigation, cruise, arrival, parking.
2. `test_s02_night_tunnel_entry_and_exit`: Daylight riding, underground tunnel entry (smooth backlight fade ON), halogen lamp deadband, daylight exit (fade OFF).
3. `test_s03_phone_disconnect_and_reconnection_on_highway`: Highway ride, BLE drop, VSS continuity, phone reconnect, seamless navigation resumption.
4. `test_s04_web_portal_maintenance_and_ota_upgrade`: SoftAP connection, diagnostics query, tire pulse recalibration, OTA binary flash.
5. `test_s05_battery_alternator_fault_and_limp_warning`: Alternator regulator breakdown, voltage drops below 11.2V, low-voltage warning flag.
6. `test_s06_vss_wire_break_instant_gps_fallback`: Physical pulse wire severed at speed, instant GPS fallback.
7. `test_s07_heavy_city_traffic_multi_app_alerts`: Multi-app notifications (WhatsApp, call ringer, turn instructions) multiplexed on 84x48 LCD.
8. `test_s08_monsoon_rain_and_high_humidity_climate`: Extreme tropical rainstorm climate readout.
9. `test_s09_multi_day_clock_stability_and_drift_correction`: Long-term clock drift correction across days.
10. `test_s10_factory_reset_and_nvs_defaults_restore`: Web UI factory reset restores baseline configuration.

---

## 3. Runner Invocation Guide

The master test runner is located at `tests/e2e/run_e2e_tests.sh`. It automatically identifies the Python environment (using `/home/nixon/.pyenv/versions/3.11.14/bin/python3` or system `python3`), ensures `PYTHONPATH` includes `tests/e2e/`, and executes requested suites.

### Execution Commands:

```bash
# 1. Run the entire test suite (all 197 test cases across Tiers 1-4)
./tests/e2e/run_e2e_tests.sh --all

# 2. Run Tier 1 Feature Coverage suite (F01 through F24)
./tests/e2e/run_e2e_tests.sh --tier1

# 3. Run Tier 2 Boundary & Corner Cases suite
./tests/e2e/run_e2e_tests.sh --tier2

# 4. Run Tier 3 Cross-Feature Combinatorial suite
./tests/e2e/run_e2e_tests.sh --tier3

# 5. Run Tier 4 Real-World Application Scenarios suite
./tests/e2e/run_e2e_tests.sh --tier4

# 6. Progressive Milestone Runs (for incremental implementation verification):
./tests/e2e/run_e2e_tests.sh --m1   # Toolchain, Isolation, Partitions
./tests/e2e/run_e2e_tests.sh --m2   # PCD8544 Display & LEDC Backlight
./tests/e2e/run_e2e_tests.sh --m3   # NimBLE Chronos Protocol Decoders
./tests/e2e/run_e2e_tests.sh --m4   # Hardware Sensors & GPS Fallback
./tests/e2e/run_e2e_tests.sh --m5   # Web Portal, REST APIs, Web OTA
./tests/e2e/run_e2e_tests.sh --m6   # System Integration & Acceptance Build
```

---

## 4. Feature Coverage Mapping Matrix

| Feature | Feature Name | Tier 1 Suite | Test Methods Count | Tier 2 Boundaries | Tier 3 Interactions | Tier 4 Scenarios | Total Coverage |
| :--- | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **F01** | Toolchain & Isolation | `test_toolchain_and_partitions.py` | 5 | Yes | - | - | 5 |
| **F02** | `source_idf.sh` | `test_toolchain_and_partitions.py` | 5 | Yes | - | - | 5 |
| **F03** | 4MB Dual-OTA Partitions | `test_toolchain_and_partitions.py` | 6 | Yes | - | - | 6 |
| **F04** | Project Build Skeleton | `test_toolchain_and_partitions.py` | 5 | - | - | - | 5 |
| **F05** | PCD8544 SPI Driver | `test_display_and_ui.py` | 5 | 5 | 2 | 2 | 14 |
| **F06** | LEDC PWM Backlight | `test_display_and_ui.py` | 5 | 3 | 2 | 2 | 12 |
| **F07** | UI Status Bar Engine | `test_display_and_ui.py` | 5 | 2 | 4 | 3 | 14 |
| **F08** | UI Speedometer & Turn Icon | `test_display_and_ui.py` | 5 | 2 | 3 | 3 | 13 |
| **F09** | UI Notification Overlay | `test_display_and_ui.py` | 5 | 3 | 2 | 2 | 12 |
| **F10** | NimBLE GATT Server | `test_chronos_protocol_decoders.py` | 5 | 1 | 2 | 2 | 10 |
| **F11** | Chronos Handshake & Battery | `test_chronos_protocol_decoders.py` | 5 | 1 | 2 | 2 | 10 |
| **F12** | Navigation Info Decoder | `test_chronos_protocol_decoders.py` | 5 | 4 | 3 | 3 | 15 |
| **F13** | Navigation Icon Decoder | `test_chronos_protocol_decoders.py` | 5 | 2 | 2 | 2 | 11 |
| **F14** | Notifications Decoder | `test_chronos_protocol_decoders.py` | 5 | 3 | 2 | 2 | 12 |
| **F15** | Time Sync Decoder | `test_chronos_protocol_decoders.py` | 5 | 2 | 2 | 2 | 11 |
| **F16** | Aki Voltmeter ADC | `test_sensors_and_fallback.py` | 5 | 4 | 2 | 2 | 13 |
| **F17** | Speedometer VSS & Fallback | `test_sensors_and_fallback.py` | 5 | 4 | 3 | 3 | 15 |
| **F18** | DHT11/DHT22 Sensor | `test_sensors_and_fallback.py` | 5 | 4 | 2 | 2 | 13 |
| **F19** | LDR Light Sensor & Auto BL | `test_sensors_and_fallback.py` | 5 | 4 | 2 | 2 | 13 |
| **F20** | Wi-Fi SoftAP | `test_web_portal_and_ota.py` | 5 | 1 | 2 | 2 | 10 |
| **F21** | Embedded Web Portal | `test_web_portal_and_ota.py` | 5 | 1 | 2 | 2 | 10 |
| **F22** | REST APIs | `test_web_portal_and_ota.py` | 5 | 6 | 2 | 2 | 15 |
| **F23** | Web OTA Update | `test_web_portal_and_ota.py` | 5 | 4 | 2 | 2 | 13 |
| **F24** | System Integration & Build | `test_system_integration_and_build.py` | 5 | 1 | 1 | 1 | 8 |
| **TOTAL**| **All 24 Features** | **9 Suites** | **121** | **50** | **16** | **10** | **197** |

---

## 5. Test File Index

All test code is strictly located within `tests/e2e/`:
- `e2e_framework.py`: Shared reference models, mathematical algorithms, Chronos packet generator, and PCD8544 display emulator.
- `test_toolchain_and_partitions.py`: Tier 1 tests for F01, F02, F03, F04.
- `test_display_and_ui.py`: Tier 1 tests for F05, F06, F07, F08, F09.
- `test_chronos_protocol_decoders.py`: Tier 1 tests for F10, F11, F12, F13, F14, F15.
- `test_sensors_and_fallback.py`: Tier 1 tests for F16, F17, F18, F19.
- `test_web_portal_and_ota.py`: Tier 1 tests for F20, F21, F22, F23.
- `test_system_integration_and_build.py`: Tier 1 tests for F24.
- `test_tier2_boundaries_and_corners.py`: Tier 2 boundary and corner case test suite (50 test cases).
- `test_tier3_cross_feature_interactions.py`: Tier 3 combinatorial and interaction test suite (16 test cases).
- `test_tier4_real_world_scenarios.py`: Tier 4 end-to-end real world application scenarios (10 test cases).
- `run_e2e_tests.sh`: Master runner shell script.
