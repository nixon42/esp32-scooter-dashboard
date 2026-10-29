# ESP32 Scooter Dashboard (Honda Vario 125 K60)

Smart Heads-Up Display (HUD) & Auxiliary Digital Dashboard untuk Honda Vario 125 K60 bertenaga **ESP32-C3 SuperMini (RISC-V)** dengan layar transflektif **Nokia 5110 (PCD8544 LCD)**, integrasi **BLE Chronos Smartwatch**, pembaca sensor motorik real-time, Web Portal, dan sistem Dual-OTA update.

---

## 🌟 Fitur Utama

1. **Transreflective LCD (PCD8544 / Nokia 5110):**
   - Sangat terbaca di bawah terik sinar matahari langsung (*glanceable daylight readability*).
   - Auto-brightness backlight (PWM LEDC) via sensor LDR / ambient light.
   - Pilihan tampilan jam Standby (Digital Font, Analog Nokia Clock, Weather Dashboard).

2. **Integrasi BLE Chronos (Navigasi & Notifikasi Android):**
   - **Turn-by-turn Navigation:** Tampilan arah panah belokan, jarak, dan nama jalan real-time sinkron dari Google Maps via aplikasi Chronos.
   - **Cruising Mode:** Jam digital besar + kompas ringkas saat rute lurus.
   - **Notifikasi Pintar & Caller ID:** Pop-up pesan (WhatsApp, Telegram, SMS, dll.) dan panggilan telepon dengan getar/buzzer audio tanpa mengganggu routing navigasi.
   - **Indikator Koneksi BT & Baterai HP:** Tampilan status Bluetooth bindrune tajam di Screen Clock dan baterai HP tersimpan murni di RAM (*zero flash wear*).

3. **Sensor Terintegrasi:**
   - **Voltmeter Aki Motor:** Deteksi tegangan aki 12V (Warning aki soak < 11.8V dan overcharge > 14.8V).
   - **Speedometer VSS:** Membaca pulsa sensor kecepatan bawaan motor (kabel pink/green Vario 125).
   - **Suhu & Kelembapan Udara:** Sensor DHT11 eksternal pada pod tahan hujan.
   - **Audio Buzzer Multi-Profil:** Pilihan nada Modern Chirp, Soft Chime, Sci-Fi Arcade, Minimal Beep, dan Nokia Classic (Gran Vals & SMS Tone).

4. **Konektivitas WiFi & Dual-OTA:**
   - SoftAP Web Portal (`192.168.4.1`) untuk kalibrasi sensor, ganti profil buzzer, dan live dashboard.
   - Web OTA Update: Upload firmware `.bin` langsung lewat browser HP/laptop tanpa kabel USB.

5. **Desain Casing 3D (CAD/DFAM):**
   - Labyrinth seal tahan air (IP54+) siap cetak PETG di Bambu Lab / printer 3D standar.
   - Bracket spion kokoh dan pod sensor DHT11 rear-mounted.

---

## 🔌 Pinout & Skematik Wiring (ESP32-C3 SuperMini)

| Komponen / Sinyal | Pin ESP32-C3 | Keterangan |
| :--- | :--- | :--- |
| **LCD SCK** | `GPIO 6` | SPI Clock Nokia 5110 |
| **LCD MOSI (DIN)** | `GPIO 7` | SPI Data Nokia 5110 |
| **LCD CS (SCE)** | `GPIO 8` | Chip Select |
| **LCD DC** | `GPIO 4` | Data / Command Select |
| **LCD RST** | `GPIO 10` | Hardware Reset |
| **LCD BL (Backlight)** | `GPIO 2` | PWM Kontrol Backlight (via Transistor NPN / 2N2222) |
| **Voltmeter Aki** | `GPIO 0` | ADC1_CH0 (Voltage Divider: 100kΩ ke 12V, 20kΩ ke GND) |
| **LDR Sensor** | `GPIO 1` | ADC1_CH1 (Divider LDR + 10kΩ ke 3.3V) |
| **DHT11 (Temp/Hum)** | `GPIO 3` | 1-Wire Data (Pull-up resistor 4.7kΩ ke 3.3V) |
| **VSS Speedometer** | `GPIO 5` | Pulsa VSS Vario (Level shifter / Optocoupler ke 3.3V) |
| **Buzzer** | `GPIO 20` | Output nada / nada dering (Active DC / Passive PWM) |

---

## 🛠️ Cara Build Firmware

### Menggunakan ESP-IDF v5.3 Lokal
```bash
# 1. Load environment ESP-IDF
source source_idf.sh

# 2. Set target chip ESP32-C3
idf.py set-target esp32c3

# 3. Compile firmware
idf.py build

# 4. (Opsional) Jalankan E2E test suite
./tests/e2e/run_e2e_tests.sh --all
```

### Menggunakan GitHub Actions (CI/CD Otomatis)
Repositori ini dilengkapi GitHub Actions yang otomatis:
1. Meng-compile firmware setiap ada push ke branch `main` atau pembuatan git tag (`v*`).
2. Menghasilkan:
   - `esp32-scooter-dashboard.bin`: Untuk update via Web OTA browser.
   - `esp32-scooter-dashboard-merged.bin`: Single binary (bootloader + partition table + app) untuk flashing awal kabel USB dari offset `0x0`.
3. Mengupload hasil rilis otomatis ke tab **Releases** di GitHub.

---

## 🚀 Cara Flash Firmware

### Opsi A: Update OTA via Web Portal (Tanpa Kabel)
1. Nyalakan dashboard dan hubungkan HP ke WiFi SoftAP dashboard (`ESP32-Scooter-HUD`).
2. Buka browser ke alamat `http://192.168.4.1/ota`.
3. Pilih file `esp32-scooter-dashboard.bin` dan klik **Update**.

### Opsi B: Flashing Pertama Kali via USB (esptool)
```bash
esptool.py --chip esp32c3 -b 460800 write_flash 0x0 esp32-scooter-dashboard-merged.bin
```

---

## 📄 Lisensi
MIT License.
