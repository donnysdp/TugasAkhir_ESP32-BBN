# Evaluasi Retrospektif & Improvement Suggestion - Tugas Akhir ESP32-BBN

Folder ini berisi perbaikan kode sumber (*source code*) Tugas Akhir **Body-to-Body Networks (BBN) menggunakan ESP32** berdasarkan analisis evaluasi dari model **Gemini 3.5 Flash (Medium)**.

Perbaikan utama yang kami lakukan berfokus pada transisi ke arsitektur **FreeRTOS Dual-Core** pada ESP32, untuk mengeliminasi gangguan transmisi data jaringan mesh akibat pembacaan sensor yang memblokir (*blocking loop*).

---

## 🚀 Rangkuman Perbaikan Utama (Improvements)

### 1. Arsitektur Dual-Core FreeRTOS (`src/main_node.cpp`)
ESP32 memiliki prosesor dual-core (Core 0 dan Core 1). Pada kode bawaan Arduino biasa, semua program berjalan pada satu thread tunggal di Core 1 secara berurutan. Di sini, kami membaginya menjadi dua tugas independen (*tasks*):
* **`SensorTask` (Core 1 - App Core, Prioritas Tinggi)**: Bertugas membaca buffer FIFO sensor MAX30102 secara berkala tanpa interupsi, menghitung SpO2, dan mendeteksi denyut jantung (Heart Rate).
* **`MeshTask` (Core 0 - Protocol Core, Prioritas Rendah)**: Bertugas mengeksekusi `mesh.update()` untuk memproses paket data WiFi dan menjaga sinkronisasi waktu jaringan mesh. Jaringan mesh ditaruh di Core 0 karena berbagi core dengan stack internal WiFi (TCP/IP stack) dari sistem operasi ESP32, yang secara default berada di Core 0.

### 2. Thread-Safety dengan Mutex
Karena data SpO2, Heart Rate, dan pesan string (`msg`) dibaca oleh `MeshTask` (untuk dikirim secara broadcast) dan ditulis oleh `SensorTask` (berdasarkan sensor), kami menggunakan **FreeRTOS Mutex Semaphore** (`xDataMutex`) untuk menjamin tidak terjadi tabrakan memori atau korupsi data (*race condition*) ketika kedua core mengakses variabel yang sama secara bersamaan.

### 3. Proteksi Pembagian dengan Nol (*Division by Zero*)
Kami menambahkan pengaman logika pada perhitungan indeks SpO2:
```cpp
if (avered > 0.0 && sumirrms > 0.0 && aveir > 0.0) {
    double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
    // ...
}
```
Ini mencegah variabel `R` bernilai `NaN` atau `Infinity` saat sensor tidak menerima cahaya pantul yang valid (misalnya ketika jari dilepas mendadak).

### 4. Monitor Kesehatan Heap Memory (`src/main_root.cpp`)
Pada node jembatan (Root Node), kami menambahkan log debugger untuk memonitor ketersediaan RAM bebas (*Free Heap*) sebelum dan sesudah menangani request JSON pada route `/scan` untuk mendeteksi potensi kebocoran memori (*memory leak*).

---

## 📂 Struktur Folder Proyek

* [platformio.ini](file:///C:/Users/donny/TugasAkhir_ESP32-BBN_improvement/platformio.ini): File konfigurasi dependency library PlatformIO.
* [src/main_node.cpp](file:///C:/Users/donny/TugasAkhir_ESP32-BBN_improvement/src/main_node.cpp): Kode Node Sensor kesehatan berbasis FreeRTOS.
* [src/main_root.cpp](file:///C:/Users/donny/TugasAkhir_ESP32-BBN_improvement/src/main_root.cpp): Kode Root Node/Gateway Bridge dengan debugger memory.

---

## 🛠️ Cara Membuat Branch dan Mengunggah Perubahan Ini ke GitHub Anda

Karena asisten AI tidak memiliki akses ke kredensial/token GitHub pribadi Anda demi alasan keamanan, Anda dapat mengunggah file-file ini ke repositori Anda sendiri dengan mengikuti langkah mudah berikut:

1. **Buka Terminal** (Command Prompt / Git Bash) di komputer Anda.
2. Arahkan ke folder repositori lokal Anda yang terhubung dengan GitHub (misalnya tempat Anda melakukan clone di tahun 2022).
3. Buat branch baru bernama `improvement-gemini3.5-flash(medium)` dan berpindah ke branch tersebut:
   ```bash
   git checkout -b "improvement-gemini3.5-flash(medium)"
   ```
4. Copy/ganti isi file di folder lokal lama Anda dengan file baru dari folder ini:
   * Ganti `platformio.ini`
   * Letakkan `main_node.cpp` dan `main_root.cpp` ke folder `src/` (sesuaikan penamaan file proyek Anda).
5. Lakukan commit terhadap perubahan tersebut:
   ```bash
   git add .
   git commit -m "Add FreeRTOS dual-core optimization and safety improvements by Gemini 3.5 Flash"
   ```
6. Push branch baru ini ke GitHub Anda:
   ```bash
   git push origin "improvement-gemini3.5-flash(medium)"
   ```

Setelah langkah 6 selesai, branch baru Anda akan muncul di GitHub dan Anda siap membandingkannya dengan model AI lainnya!
