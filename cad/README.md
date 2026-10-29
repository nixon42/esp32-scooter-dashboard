# 🏍️ Scooter Dashboard 3D Enclosure & Mirror Mount (v4.2 Solid-Verified)
**Khusus Dirancang & Dioptimalkan untuk Bambu Lab A1 Mini (PETG)**
**Hardware Fastener: 100% Menggunakan Baut, Mur, & Ring Plat M4!**

Model 3D CAD siap cetak (*watertight manifold STL*) yang dirancang khusus untuk motor skuter harian dengan pemasangan di **baut spion (M10)**, tahan getaran mesin/jalan berlubang, dan **100% kedap air hujan**.

---

## 🔩 Baut, Mur & Aksesoris Perakitan (Serba M4)

Semua kebutuhan baut perakitan kini diseragamkan ke **M4** sehingga Anda tidak perlu repot mencari ukuran baut yang berbeda-beda:

1. **Baut & Mur Casing (Pengunci Depan-Belakang):**
   - **4x Baut M4 × 25 mm** (atau M4 × 30 mm, Socket Head Cap Screw / Hex / Obeng).
   - **4x Mur M4 Hex** (masuk ke lubang heksagonal di punggung `main_housing`, ukuran $7.0\text{ mm}$ antar sisi datar). Lubang tembus terbuka bersih 100% di belakang (*flush insertion*).
   - *(Opsional)* 4x Ring Plat M4 jika menggunakan baut kepala non-tanam.

2. **Baut & Mur Engsel Spion (Pengunci Kemiringan Dashboard):**
   - **1x Baut M4 × 25 mm** (atau M4 × 30 mm).
   - **1x Mur M4 Hex** (atau Mur M4 Nyloc anti-kendor) -> langsung terkunci di *hex nut trap* pada lengan braket.
   - **1x Ring Plat M4** -> masuk ke ceruk ring $\varnothing 10.0\text{ mm}$ di sisi kepala baut.
   - **Clearance Artikulasi:** Terverifikasi bebas tabrakan ($0.0000\text{ mm}^3$ collision) pada rentang tilt $-25^\circ$ hingga $+25^\circ$.

3. **Pemasangan di Motor:**
   - **Baut Spion M10 Standar Bawaan Motor** (Honda, Yamaha, Suzuki, Vespa).
   - Lubang pelat braket berdiameter $\varnothing 10.5\text{ mm}$ dengan ceruk ring $\varnothing 22.4\text{ mm}$ dan akses vertikal kunci soket bebas 50 mm+.

---

## 🛠️ Ringkasan Update Desain v4.2

1. **Lubang Baut M4 Presisi & Terbuka Bersih:**
   - Lubang tembus baut casing & engsel: $\varnothing 4.4\text{ mm}$ (toleransi cetak pas untuk drat M4).
   - Lubang tanam kepala baut (*counterbore*) pada `front_bezel`: $\varnothing 8.0\text{ mm}$, kedalaman $3.8\text{ mm}$.
   - Lubang mur tanam (*hex nut pocket*) pada `main_housing`: lebar datar $7.2\text{ mm}$, kedalaman $5.2\text{ mm}$, menembus permukaan belakang 100% bersih tanpa ada lapisan tipis yang menghalangi.
2. **Engsel Tilt Bebas Hambatan (`mirror_bracket.stl` & `main_housing.stl`):** Leher braket dirampingkan ($\le 11.2\text{ mm}$) sehingga kuping engsel bodi ($12.2\text{ mm}$ gap) dapat berputar bebas $\pm 25^\circ$ dengan clearance bilateral $0.5\text{ mm}$.
3. **Braket Spion M10 Terbuka 100% (`mirror_bracket.stl`):** Akses kunci sok dan mur spion 360° bebas hambatan.
4. **Pod Sensor DHT11 Terintegrasi Langsung ke Bodi Utama (`dht11_mount.stl`):** Mengunci langsung via *slide-dock* di bawah bodi utama.
5. **Bezel Depan Bersih (`front_bezel.stl`):** Hanya 1 jendela layar LCD, tanpa lubang LDR terpisah (LDR membaca tembus akrilik bening dari dalam).
6. **Tatakan Kompartemen Internal Rapi (`main_housing.stl`):** Cradle mandiri untuk ESP32-C3 SuperMini dan MP1584EN, sekat isolasi 12V vs 3.3V, rak keliling 3 sisi untuk Nokia 5110, dan jembatan pengikat kabel ties.

---

## 📁 Daftar File CAD (di folder `cad/`)

| File | Fungsi | Volume | Status Mesh |
| :--- | :--- | :--- | :--- |
| **`front_bezel.stl`** | Bezel depan flat (lubang tanam baut M4, 1 jendela LCD) | 12.3 cm³ | Watertight (1 Solid) |
| **`main_housing.stl`** | Rumah utama (sarang mur M4 di belakang, engsel M4 pivot, tatakan) | 42.5 cm³ | Watertight (1 Solid) |
| **`mirror_bracket.stl`** | Braket spion M10 monolitik (engsel M4 + sarang mur M4) | 26.1 cm³ | Watertight (1 Solid) |
| **`dht11_mount.stl`** | Pod DHT11 (klip geser ke bodi utama + slot kabel ties) | 5.8 cm³ | Watertight (1 Solid) |
| `scooter_dashboard.scad` | Source OpenSCAD parametrik v4.1 (M4 unified) | - | Source Code |
| `generate_cad.py` | Generator Python (Manifold3D + Trimesh) | - | Generator Script |

---

## ⚙️ Panduan Cetak di Bambu Studio (A1 Mini)

| Bagian | Orientasi Cetak Terbaik | Infill | Wall Loops | Support |
| :--- | :--- | :--- | :--- | :--- |
| **`front_bezel.stl`** | **Muka depan menghadap ke bawah** (nempel di Textured PEI Plate agar permukaan bertekstur matte rapi). | 30% Gyroid | 4 Walls | Tidak perlu |
| **`main_housing.stl`** | **Bukaan depan menghadap ke atas** (dasar casing nempel di bed). | 40% Gyroid | 4-5 Walls | Tree Support hanya untuk kuping engsel luar |
| **`mirror_bracket.stl`** | **Tidur miring pada sisi samping (bidang X)** ATAU berdiri dengan brim. | **100% Solid** | **6 Walls** | Tree support jika berdiri |
| **`dht11_mount.stl`** | **Punggung datar menempel di bed** (kisi menghadap ke atas). | 30% Gyroid | 3 Walls | Tidak perlu |
