# Agent Guide & Onboarding: ESP32 Scooter Dashboard (VarioHUD)

Panduan operasional dan referensi teknis wajib untuk **AI Coding Assistant (Agent)** atau developer baru yang bekerja di repositori ini.

---

## 1. Identitas & Visi Proyek

- **Nama Proyek:** VarioHUD (ESP32 Scooter Dashboard)
- **Target Kendaraan:** Honda Vario 125 Old LED (K60)
- **Target Hardware:** ESP32-C3 SuperMini (RISC-V 32-bit, 4MB Flash)
- **Display:** Nokia 5110 LCD (PCD8544 84x48 piksel, SPI Master)
- **Framework Firmware:** Native **ESP-IDF v5.3** (C/FreeRTOS, bukan Arduino)
- **Aplikasi Pendamping HP:** Android **Chronos** (Apache NimBLE NUS GATT)

---

## 2. Aturan Kritis Sistem & Keselamatan Lingkungan (*Must-Follow*)

> [!CAUTION]
> **ATURAN MUTLAK RUANG DISK & PARTISI ROOT (`/`):**
> Partisi root `/` memiliki ruang penyimpanan terbatas. **JANGAN PERNAH** menginstall tools, paket pip, atau cache ke direktori root seperti `~/.espressif`, `~/.cache`, atau `/tmp` tanpa kontrol.
> 
> - Seluruh ESP-IDF toolchain & virtualenv tersimpan secara terisolasi di NVMe mount:  
>   `/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/.espressif`
> - Python interpreter utama yang digunakan adalah Python 3.11:  
>   `/home/nixon/.pyenv/versions/3.11.14/bin/python3`
> - Script pemuat environment wajib dijalankan sebelum memanggil `idf.py`:  
>   `source source_idf.sh`

---

## 3. Filosofi Desain Inti (*Core Design Principles*)

### A. 100% Zero-Button Architecture (Tanpa Tombol Fisik)
Pengendara motor fokus pada stang dan jalan raya. **Dilarang menambahkan fitur atau interaksi yang mewajibkan penekanan tombol fisik.**
- Seluruh pergantian layar berlangsung secara otomatis berdasarkan state & event:
  $$\text{Panggilan Masuk (High)} > \text{Notifikasi Pesan (Auto-dismiss 7s)} > \text{Maps Navigasi} > \text{Smartwatch Watchface (Idle)}$$
- Ketika telepon ditolak/selesai di HP, modal langsung menutup seketika.
- Ketika notifikasi pesan masuk, layar menampilkan pesan selama 7 detik lalu otomatis tertutup sendiri (*auto-dismiss*).

### B. Persistent Global Header Bar ($y=0..7$, divider $y=8$)
Tiga informasi esensial smartphone dan motor **WAJIB selalu tampil di seluruh layar tanpa terkecuali**:
1. **Ikon Bluetooth BLE** (`glyph_ble` di $x=0..4$)
2. **Jam Digital Real-Time** (`12:45` di $x=7..35$)
3. **Tegangan Aki Motor** (`12.4V` di $x=37..58$)
4. **Sisa Baterai Smartphone** (`85%` atau `85%+` jika charging di $x=62..82$)

### C. Multi-Tasking Terpadu (Maps + Musik / Panggilan / Pesan)
- Saat Maps aktif bersamaan dengan Musik: Tampilkan belokan 24x24 di kiri, telemetri di kanan, dan **music ticker** di baris bawah ($y=38..47$).
- Saat Maps aktif dan ada Panggilan/Pesan: Tampilkan modal panggilan/pesan dengan **Mini-Nav Turn Reminder** (`[Nav: 250m Belok]`) agar pengendara tidak salah belok di jalan saat HP berdering.

### D. Thread Safety State Repository
Seluruh telemetri sistem dikelola secara terpusat dan thread-safe di `hud_state.h/c` menggunakan FreeRTOS Mutex.
- **DILARANG** mengakses variabel global state secara langsung antar-task.
- Gunakan selalu snapshot terisolasi: `hud_state_get_snapshot(&snap)` dan `hud_state_update(&snap)` atau setter fungsi khusus seperti `hud_state_set_vss_speed()`, `hud_state_set_phone_battery()`, dll.

---

## 4. Perintah Tooling & Alur Kerja (*Workflow Commands*)

### A. Kompilasi Firmware ESP-IDF
Gunakan bash subshell untuk memuat environment dari NVMe:
```bash
bash -c "source source_idf.sh && idf.py build"
```
Output biner firmware berada di `build/esp32-scooter-dashboard.bin`.

### B. Menjalankan Rangkaian Test E2E
Seluruh test suite python berada di `tests/e2e/`:
```bash
# Menjalankan seluruh test suite otomatis:
/home/nixon/.pyenv/versions/3.11.14/bin/python3 -m unittest discover -s tests/e2e -p 'test_*.py'

# Atau jalankan script master runner:
bash tests/e2e/run_e2e_tests.sh
```

### C. Flashing Firmware ke Hardware Fisik
```bash
bash -c "source source_idf.sh && idf.py -p /dev/ttyACM0 flash monitor"
```

---

## 5. Jebakan Teknis Umum (*Gotchas & Pitfalls*)

1. **GCC `-Werror=format-truncation=`:**
   ESP-IDF mengaktifkan peringatan GCC sebagai error fatal. Ketika menggunakan `snprintf()`, pastikan buffer tujuan memiliki ukuran yang cukup untuk menampung panjang string maksimum dari field sumber.
   - *Contoh:* Jika menggabungkan `title` (64B) dan `artist` (48B), gunakan `char combined[128];` bukan `char combined[48];`.

2. **Karakter Hex Escape Sequence Collision (`\x7F`):**
   Pada compiler GCC C99/C11, string literal `"28\x7FC"` akan mencoba membaca `7FC` sebagai nilai hex integer tunggal sehingga menghasilkan error `warning: hex escape sequence out of range`.
   - *Solusi:* Pisahkan string literal: `"%2.0f\x7F" "C"`.

3. **Batasan Panjang Key NVS:**
   Key di NVS ESP-IDF dibatasi maksimal **15 karakter ASCII**. Jangan membuat key lebih panjang dari 15 karakter saat menambah konfigurasi baru.

4. **Ukuran Framebuffer Nokia 5110:**
   PCD8544 memiliki resolusi 84x48 piksel. Framebuffer tepat berukuran **504 byte** ($84 \times 6$ bank byte vertikal). Setiap byte mewakili 8 piksel dalam satu kolom vertikal (bit 0 adalah baris teratas, bit 7 baris terbawah).

---

## 6. Checklist Eksekusi Tugas (*Agent Task Checklist*)

Sebelum menyelesaikan pekerjaan atau membalas user:
- [ ] Apakah `hud_state.h` dan `hud_state.c` sudah diperbarui dan thread-safe?
- [ ] Apakah parser paket `chronos_packet.c` menangani opcode terkait tanpa overflow?
- [ ] Apakah layout visual `hud_ui.c` tetap mematuhi batas $84 \times 48$ piksel dan header global $y=0..8$ tetap utuh?
- [ ] Apakah unit test / E2E test di `tests/e2e/` sudah ditambahkan/diperbarui?
- [ ] Apakah seluruh test lolos 100% via `run_e2e_tests.sh`?
- [ ] Apakah kompilasi ESP-IDF sukses tanpa warning/error via `idf.py build`?
- [ ] Apakah dokumentasi pada `walkthrough.md` dan `project.md` sudah disinkronkan?
