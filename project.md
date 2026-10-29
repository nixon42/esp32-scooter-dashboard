# Project Architecture & Specification: ESP32 Scooter Dashboard (VarioHUD)

DIY Smart Motorcycle HUD & Telemetry Dashboard native firmware based on **ESP-IDF v5.3** tailored for **Honda Vario 125 Old LED (K60)**, built on **ESP32-C3 SuperMini (RISC-V)** and **Nokia 5110 LCD (PCD8544 84x48)**.

---

## 1. Hardware Pinout Table (ESP32-C3 SuperMini)

Zero pin conflicts across all hardware subsystems:

| Pin GPIO | Function / Peripheral | Conditioning & Circuit Details | Status |
|---|---|---|---|
| **GPIO 0** | **Voltmeter Aki** (`ADC1_CH0`) | Pembagi tegangan 10 kΩ + 2.2 kΩ ke GND + diode Zener 3.3V pengaman. Rasio pengali nominal 5.54x (0-18V DC). | *Opsional (Web toggle)* |
| **GPIO 1** | **Sensor Cahaya LDR** (`ADC1_CH1`) | LDR fotoresistor + resistor 10 kΩ ke GND sebagai pembagi tegangan (Auto Backlight). | *Opsional (Web toggle)* |
| **GPIO 2** | **LCD Backlight PWM** (`LEDC`) | Kontrol kecerahan backlight LCD Nokia 5110 (5 kHz PWM, 8-bit timer 0..255). Active-low / active-high configurable. | **Wajib** |
| **GPIO 3** | **Sensor Suhu & Kelembaban** (GPIO) | Single-wire bus DHT11 / DHT22 dengan pull-up resistor 4.7 kΩ–10 kΩ ke 3.3V. | *Opsional (Web toggle)* |
| **GPIO 4** | **Nokia 5110 DC** (Data/Command) | Sinyal kontrol Data / Command PCD8544 SPI. | **Wajib** |
| **GPIO 5** | **Speedometer Pulsa VSS** (ISR) | Sadap kabel sensor kecepatan Vario K60 (Pink/Hijau) melalui pembagi 10 kΩ + 4.7 kΩ + Zener 3.3V. Filter debounce software 250 µs di ISR. | *Opsional (Fallback ke BLE GPS)* |
| **GPIO 6** | **Nokia 5110 SCK** (SPI Master) | SPI Bus Clock (4 MHz). | **Wajib** |
| **GPIO 7** | **Nokia 5110 MOSI** (SPI Master) | SPI Bus Data (DIN). | **Wajib** |
| **GPIO 8** | **Nokia 5110 CS** (Chip Select) | Active-Low SPI Chip Select (CE). | **Wajib** |
| **GPIO 10**| **Nokia 5110 RST** (Reset) | Display hardware reset line. | **Wajib** |
| **GPIO 20/21**| **UART Serial (RX / TX)** | Port serial USB native untuk flash firmware dan monitor debug console. | **Sistem** |

---

## 2. Directory Structure

```
esp32-scooter-dashboard/
├── CMakeLists.txt                # Root CMake project configuration
├── partitions.csv                # 4MB Dual-OTA partition table
├── source_idf.sh                 # Environment loader script (loads ESP-IDF v5.3 from NVMe)
├── project.md                    # File ini (dokumentasi lengkap arsitektur & hardware)
├── agent.md                      # Panduan instruksi, tooling, dan aturan untuk AI Agent
├── main/
│   ├── CMakeLists.txt            # Main component build rules and source registrations
│   ├── main.c                   # System startup, task spawning, boot sequence coordinator
│   ├── hud_state.c              # Thread-safe telemetry state repository & NVS persistence
│   ├── include/
│   │   ├── hud_state.h          # Telemetry state struct (hud_state_t) and thread-safe APIs
│   │   ├── hud_ui.h             # UI rendering engine declarations & coordinate bounds
│   │   ├── pcd8544.h            # Nokia 5110 SPI Master display driver headers
│   │   ├── boot_logos.h         # 20x20 1-bit monochrome boot logo bitmaps (Vario, Android, ESP, Tux, Arch)
│   │   ├── chronos_ble.h        # Apache NimBLE GATT server profile declarations
│   │   ├── chronos_packet.h     # Chronos BLE protocol parser & encoder headers
│   │   ├── voltmeter.h          # ADC OneShot voltmeter headers & eFuse calibration
│   │   ├── speedometer.h        # VSS GPIO interrupt speed calculator headers
│   │   ├── dht_sensor.h         # DHT11/DHT22 single-wire climate sensor headers
│   │   ├── ldr_sensor.h         # LDR light sensor ADC & auto-dimmer headers
│   │   └── web_portal.h         # Wi-Fi SoftAP, HTTP REST endpoints & Web OTA headers
│   ├── display/
│   │   ├── pcd8544.c            # PCD8544 SPI driver & 504-byte bank-oriented framebuffer
│   │   ├── hud_ui.c             # Multi-tasking UI rendering engine (Header, Watchface, Nav, Call, Notif)
│   │   └── font5x7.h            # 5x7 column-major font, glyph_ble, glyph_battery, glyph_music
│   ├── ble/
│   │   ├── chronos_ble.c        # Apache NimBLE GATT NUS server (6e400001...)
│   │   └── chronos_packet.c     # Packet decoder: 0x91 (Battery), 0x9D (Music), 0xEF (Nav), 0xEE (Icon), 0x72 (Notif)
│   ├── sensors/
│   │   ├── voltmeter.c          # ADC1_CH0 battery voltage reader with calibration
│   │   ├── speedometer.c        # VSS pulse counter ISR (250µs debounce) + BLE GPS fallback
│   │   ├── dht_sensor.c         # DHT single-wire bitbang with microsecond timing
│   │   └── ldr_sensor.c         # ADC1_CH1 reading with ±200 mV hysteresis filter
│   └── web/
│       ├── web_portal.c         # Wi-Fi AP ('VarioHUD-Setup'), HTTP handlers (/api/status, /api/config, /update)
│       └── index_html.h         # Single-Page App (SPA) dark HUD dashboard with Web OTA AJAX uploader
└── tests/
    └── e2e/                     # Python-based full-system E2E test framework & test suites
        ├── e2e_framework.py     # Reference models (PCD8544 display, Chronos decoder, sensor math, Web server)
        ├── run_e2e_tests.sh     # Master test runner script
        ├── test_display_and_ui.py
        ├── test_chronos_protocol_decoders.py
        ├── test_sensors_and_fallback.py
        ├── test_web_portal_and_ota.py
        ├── test_system_integration_and_build.py
        ├── test_boot_screen_and_logos.py
        ├── test_tier2_boundaries_and_corners.py
        ├── test_tier3_cross_feature_interactions.py
        └── test_tier4_real_world_scenarios.py
```

---

## 3. Visual Layout & UI Geometry (Nokia 5110: 84x48 Piksel)

Layar Nokia 5110 terdiri dari 84 kolom horizontal ($x=0..83$) dan 48 baris vertikal ($y=0..47$) yang dibagi menjadi 6 bank byte vertikal (504 byte total framebuffer).

### A. Persistent Global Header Bar ($y=0..7$, divider di $y=8$)
**SELALU tampil di SEMUA layar tanpa pernah tertutup:**
```
Normal Mode:
+--------------------------------------------------------------------------+
| [B] 12:45                   12.4V                                 [85%+] |  y=0..7
|--------------------------------------------------------------------------|  y=8

Music Playing Mode:
+--------------------------------------------------------------------------+
| [B] ♪ Bohemian..                                                  [85%+] |  y=0..7
|--------------------------------------------------------------------------|  y=8
```
- **$x=0..4$:** Ikon Bluetooth BLE 5x7 (`glyph_ble`).
- **$x=7..60$:** Glyph not balok `♪` + Judul Lagu (saat musik aktif), atau jam digital `HH:MM` + tegangan aki `12.4V` (saat musik standby).
- **$x=62..82$:** Sisa persentase baterai smartphone (`85%`, dengan tanda `+` jika sedang di-charge: `85%+`).
- **$y=8$:** Garis pemisah horizontal solid ($x=0..83$).

### B. Viewport Konten Dinamis ($y=9..47$, tinggi 39 piksel)

Sistem beroperasi dengan prinsip **100% Zero-Button Architecture**; transisi layar terjadi secara otonom berdasarkan event dan prioritas:

#### 1. Default Idle Screen: Smartwatch / Smartband Watchface
Aktif ketika motor standby atau berkendara santai tanpa navigasi aktif:
```
+--------------------------------------------------------------------------+
| [B] 12:45                   12.4V                                  [85%] |
|--------------------------------------------------------------------------|
|                                                                          |
|                 1 2  :  4 5   (7-Segment Besar 22px)                     |  y=10..31
|                                                                          |
|--------------------------------------------------------------------------|
|        SEN, 06 OKT         (Kalender / Tanggal)                          |  y=33..39
|  45 km/h  28°C  (atau ♪ Judul Lagu saat berhenti / Suhu 28°C 65%)        |  y=41..47
+--------------------------------------------------------------------------+
```
- Jam 7-segment besar 22px terpusat di layar ($x=9, 25, 45, 61$, titik dua di $x=41$).
- Baris tanggal Indonesia lengkap (`MIN`, `SEN`, `SEL`, `RAB`, `KAM`, `JUM`, `SAB`).
- Baris cerdas bawah: Menampilkan kecepatan bantu (`45 km/h 28°C`) saat berjalan, ticker musik saat berhenti & memutar lagu, atau iklim standby.

#### 2. Multi-Tasking: Maps Navigasi + Pemutar Musik (Simultan)
Aktif saat pengendara membuka Google Maps via aplikasi Chronos:
```
+--------------------------------------------------------------------------+
| [B] ♪ Bohemian..                                                   [85%] |  y=0..7
|--------------------------------------------------------------------------|  y=8
| /---\ | 250 m              (Jarak belokan tebal)                         |  y=10
| |-> | | Jl. Sudirman       (Nama jalan / instruksi turn)                 |  y=18
| \---/ | 45 km/h            (Kecepatan real-time)                         |  y=26
|--------------------------------------------------------------------------|  y=36
| 12:45  ETA 07:52           (Jam digital real-time & Estimasi Tiba Nav)   |  y=38
+--------------------------------------------------------------------------+
```
- **Header Atas ($y=0..7$):** Menampung Koneksi BLE, Judul Musik (`♪ Judul`), dan Persentase Baterai Smartphone.
- **Kiri ($x=2..25, y=10..33$):** Ikon belokan 24x24 (hasil algoritma 2:1 downsampling dari 48x48 Chronos bitmap).
- **Kanan ($x=28..83$):** Jarak belokan, nama jalan, dan kecepatan motor.
- **Bawah ($y=36..47$):** Divider di $y=36$ diikuti baris navigasi gabungan Jam dan ETA/Durasi (`12:45 ETA 07:52` atau `12:45 Dur: 22m`), sehingga navigasi memiliki ruang penuh tanpa terpotong oleh musik.

#### 3. Multi-Tasking: Maps Navigasi + Panggilan Masuk (Call Modal)
```
+--------------------------------------------------------------------------+
| [B] 12:45                   12.4V                                  [85%] |
|--------------------------------------------------------------------------|
| ============= PANGGILAN ============= (Inverse Banner)                   |  y=10..18
| Ibu (+628123456789)                   (Nama / Kontak)                    |  y=21
| [Nav: 250m Belok]                     (Mini-Nav Turn Reminder!)          |  y=30
| >>> DERING <<<                        (Status Panggilan Aktif)           |  y=39
+--------------------------------------------------------------------------+
```
- Header jam & baterai HP **TIDAK tertutup**.
- **Mini-Nav Turn Reminder:** Pengendara tidak akan tersesat saat ada telepon berdering.
- Otomatis tertutup seketika panggilan selesai diangkat/ditolak di smartphone/intercom.

#### 4. Multi-Tasking: Maps Navigasi + Notifikasi Pesan (Auto-Paging 3.5s)
```
Halaman 1/2 (Detik 0..3.5):
+--------------------------------------------------------------------------+
| [B] 12:45                   12.4V                                  [85%] |
|--------------------------------------------------------------------------|
| WA: Budi (1/2)                        (Aplikasi, Pengirim & Page Idx)    |  y=10
| Posisi di mana                        (Teks Pesan Baris 1 / Word-wrap)   |  y=20
| [Nav: 250m Rute]                      (Mini-Nav Turn Reminder!)          |  y=29
| Auto tutup: 6s                        (Countdown Auto-Dismiss)           |  y=39
+--------------------------------------------------------------------------+

Halaman 2/2 (Detik 3.5..7.0):
+--------------------------------------------------------------------------+
| [B] 12:45                   12.4V                                  [85%] |
|--------------------------------------------------------------------------|
| WA: Budi (2/2)                        (Halaman Lanjutan Pesan Panjang)   |  y=10
| bro? Aku udah..                       (Sambungan Teks Baris Lanjutan)    |  y=20
| [Nav: 250m Rute]                      (Mini-Nav Tetap Terjaga!)          |  y=29
| Auto tutup: 2s                        (Countdown Auto-Dismiss)           |  y=39
+--------------------------------------------------------------------------+
```
- **Kapasitas Buffer Ekstra Luas:** Buffer pesan diperluas menjadi **256 karakter** (sebelumnya 96), mampu menampung chat WhatsApp / email panjang tanpa terpotong di memori ESP32.
- **Auto-Paging Dinamis (hingga 4 Halaman):** Jika pesan panjang, halaman otomatis berganti bergulir (misal `(1/2)`, `(2/2)`, atau `(1/3)` s/d `(3/3)`) dengan pemenggalan kata rapi (*smart word-wrapping*), dan durasi tayang otomatis menyesuaikan secara dinamis (7s s/d 12s) agar pengendara sempat membaca seluruh isi pesan sampai tuntas.
- Pop-up pesan otomatis tertutup sendiri (*auto-dismiss*) setelah selesai tanpa perlu menekan tombol fisik apa pun di setang motor.

---

## 4. BLE Chronos Protocol & Telemetry Decoder

Protokol Nordic UART Service (NUS) Bluetooth Low Energy:
- **Service UUID:** `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- **RX Characteristic (Write / Write Without Response):** `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- **TX Characteristic (Notify):** `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

### Opcodes Didukung:
1. **`0x91` - Telemetri Baterai Smartphone:**
   - Watch meminta baterai HP via paket: `0xAB, 0x00, 0x04, 0xFE, 0x91, 0x80, 0x01`.
   - HP merespons: `0xAB, 0x00, 0x05, 0xFE/0xFF, 0x91, 0x80, charging, level_pct`.
   - Nilai diekstrak ke `hud_state.phone_battery_pct` (0..100) dan `hud_state.phone_charging`.
2. **`0x9D` - Status Pemutar Musik (Media Playback):**
   - Paket: `0xAB, len_hi, len_lo, mode, 0x9D, state, title\0, artist\0`.
   - `state`: `0x00` (stop), `0x01` (play), `0x02` (pause).
   - Menyalakan `hud_state.music_active` dan mengisi string judul & artis.
3. **`0x72` - Notifikasi Aplikasi & Panggilan:**
   - `icon == 0x01`: Panggilan masuk berdering.
   - `icon == 0x02`: Panggilan berakhir / dibatalkan.
   - `state == 0x02`: Notifikasi pesan aplikasi (WhatsApp, SMS, Telegram).
   - Deteksi otomatis notifikasi pemutar musik (Spotify, YouTube Music).
4. **`0xEF` - Navigasi Turn-by-Turn:**
   - String sequence offset 12: `title`, `duration`, `distance`, `eta`, `directions`, `speed`.
5. **`0xEE` - Navigasi Icon Chunks:**
   - 3 chunk @ 96 bytes dirakit menjadi 288-byte (48x48 1-bit) bitmap.
6. **`0x93` - Waktu Sistem:**
   - Sinkronisasi waktu real-time via `settimeofday()`.

---

## 5. Web Portal & Web OTA Firmware Update

ESP32-C3 menyiarkan Wi-Fi SoftAP:
- **SSID:** `VarioHUD-Setup` (Open / WPA2)
- **IP Gateway:** `192.168.4.1`

### REST Endpoints:
- `GET /`: Halaman antarmuka SPA responsive HTML5/CSS3/JavaScript.
- `GET /api/status`: JSON telemetri (`speed`, `voltage`, `temp`, `hum`, `ble_connected`, `phone_battery`, `phone_charging`, `music_active`, `music_title`, `music_artist`, `free_heap`, `uptime`).
- `GET /api/config`: JSON konfigurasi NVS aktif.
- `POST /api/config`: Menyimpan konfigurasi baru ke NVS (kontras LCD, logo boot, rasio voltmeter, pulsa VSS).
- `POST /update`: OTA firmware binary stream uploader dengan validasi partisi dan reboot otomatis.

---

## 6. Partition Table (4MB Dual-OTA)

Didefinisikan pada `partitions.csv`:
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1A0000,
app1,     app,  ota_1,   0x1B0000,0x1A0000,
```
- Ukuran masing-masing partisi aplikasi (`app0` & `app1`): **1,703,936 bytes (1.66 MB)**.
- Ukuran binary firmware saat ini: **1,173,296 bytes (1.12 MB)**.
- Sisa ruang kosong (*headroom*): **~31% (~530 KB)**.
