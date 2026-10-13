# FGV — BOM / DAFTAR PENGADAAN PROTOTIPE (TAHAP 3 FINAL)

**Target arsitektur:** 3 Node Sensor (Hulu / Tengah / Hilir) + 1 Central Gateway Receiver
**Disusun:** 16 Sep 2026 · **Status rekonsiliasi stok:** berdasarkan info stok internal

---

## 0. REKONSILIASI STOK (yang sudah di tangan)

| Item | Stok ada | Kebutuhan | Selisih |
| :--- | ---: | ---: | ---: |
| ESP32-S3 DevKit | 2 | — | — |
| ESP32 DevKit V1 (eks MVP) | 1 (asumsi masih hidup) | — | — |
| Modul LoRa Ra-02 SX1278 433 MHz | 1 | 4 | **beli 3** |
| Sensor JSN-SR04T | 1 | 3 | **beli 2** |
| GPS Neo-6M | 0 | 0 (dihilangkan, lihat §3) | 0 |
| Gateway | (pakai unit PALD) | 1 | lihat §3 — **tidak direkomendasikan** |

**Keputusan arsitektur:** 3 node = 2× ESP32-S3 (stok) + 1× ESP32 DevKit V1 (eks MVP, node yang sudah terbukti jalan) + 2 board baru. Gateway pakai 1 board baru (bukan meminjam PALD).

---

## 1. PRIORITAS A 

|   # | Komponen                                  | Spek                             |   Qty |   Harga satuan |
| --: | :---------------------------------------- | :------------------------------- | ----: | -------------: |
|   1 | Modul LoRa Ra-02                          | SX1278, 433 MHz, SPI             |     1 |     Rp 108.500 |
|   2 | Antena LoRa 433 MHz                       | Spring/helical, IPEX atau solder |     1 |      Rp 40.000 |
|   3 | Sensor ultrasonik                         | JSN-SR04T waterproof             |     1 |      Rp 60.900 |
|   4 | Board ESP32                               | DevKit V1 30-pin *atau* ESP32-S3 |     1 |     Rp 105.000 |
|   5 | Resistor 1kΩ + 2kΩ (10 pcs masing-masing) | Pembagi tegangan ECHO 5V→3.3V    | 1 lot |      Rp 10.000 |
|   6 | Kabel jumper dupont + kabel ferrule       | Jantan-betina, 20 cm             | 1 set |      Rp 25.000 |
|   7 | GPS                                       | GPS Neo 6M                       |     1 |       Rp68.000 |
|     |                                           |                                  | Total | **Rp 417.400** |

> ⚠️ **Catatan teknis #5 (penting):** JSN-SR04T disuplai 5V dan pin ECHO-nya mengeluarkan level ~5V. Menyambungkannya langsung ke GPIO ESP32 (3.3V) itu **di luar spesifikasi** dan bisa memperpendek umur pin. Di MVP eksisting ECHO disambung langsung ke GPIO18 — perbaiki dengan pembagi tegangan 1k/2k pada ketiga node.

---

## 2. PRIORITAS B — PENTING (testbed & demo)

|   # | Komponen                                      | Untuk                                  |   Qty |    Estimasi |       Subtotal |
| --: | :-------------------------------------------- | :------------------------------------- | ----: | ----------: | -------------: |
|   7 | Pompa air submersible                         | Sumber aliran testbed                  |     1 |   Rp 50.000 |      Rp 50.000 |
|   8 | Dimmer/potensiometer AC atau modul PWM        | Variasi debit pompa                    |     1 |   Rp 20.000 |      Rp 20.000 |
| 9 | Pipa kotak PVC 40×40 mm | Saluran uji 3 tingkat kaskade (30 cm/tingkat) | 2 batang (4 m) | Rp 35.000 | Rp 70.000 |
| 10 | Wadah besar reservoir ±1 m | Sembak/ray planter, ≥25 L | 1 | Rp 55.000 | Rp 55.000 |
| 11 | Pipa PVC 4" (potong 10 cm) + pipa PVC 2" | Tampungan tiap tingkat + stilling well sensor | 1 set | Rp 60.000 | Rp 60.000 |
| 11b | Besi hollow / kayu rangka 3 tingkat | Bingkai kaskade (dek 30/20/10 cm, susun tangga) | 1 set | Rp 80.000 | Rp 80.000 |
| 11c | Selang, fitting, knee, lem PVC | Jalur sirkulasi tertutup | 1 lot | Rp 45.000 | Rp 45.000 |
|  12 | Wadah uji skala kecil + penggaris/mistar baja | Kalibrasi level                        | 1 lot |   Rp 25.000 |      Rp 25.000 |
|  13 | Bracket aluminium + baut                      | Dudukan JSN-SR04T di atas saluran      | 3 set |   Rp 15.000 |      Rp 45.000 |
|  14 | Box panel plastik + breket                    | Housing node versi meja                |     3 |   Rp 25.000 |      Rp 75.000 |
|  15 | Penggaris/mistar baja 30 cm & waterpass       | Pengukuran geometri saluran (Mannning) |     1 |   Rp 25.000 |      Rp 25.000 |
|  16 | Gelas ukur 1 L + stopwatch                    | Ground-truth debit volumetrik          |     1 |   Rp 30.000 |      Rp 30.000 |
|     |                                               |                                        |       | **TOTAL B** | **Rp 430.000** |

---

## 3. PRIORITAS C — OPSIONAL (hanya bila ada dana & waktu)

|   # | Komponen                              | Alasan                                               |     Estimasi |
| --: | :------------------------------------ | :--------------------------------------------------- | -----------: |
|  17 | Modul CDM324 24.125 GHz               | Bench test radar Doppler. **Bukan jalur demo.**      |    Rp 50.000 |
|  18 | OP-AMP LM358 / TL072 + PCB prototipe  | Front-end penguat Doppler                            |    Rp 25.000 |
|  19 | OLED SSD1306 I2C 0.96"                | Display status lokal di tiap node (nilai demo +)     | Rp 35.000 ×3 |
|  20 | Panel surya 5V 3W + TP4056 + 2× 18650 | Demo kemandirian energi pada 1 node                  |    Rp 85.000 |
|  21 | Rain gauge tipping-bucket             | Hujan lokal aktual sebagai *leading indicator* kedua |   Rp 120.000 |
|  22 | Buzzer + LED RGB indikator level      | Alarm lokal di gateway                               |    Rp 15.000 |

---

## 4. TOTAL ANGGARAN

```
PRIORITAS A (wajib)          Rp   393.000
PRIORITAS B (testbed/demo)   Rp   430.000
------------------------------------------
MINIMAL JALAN                Rp   823.000

PRIORITAS C bila diambil semua   + Rp 360.000
ALL-IN (A+B+C)                   Rp 1.183.000
```

Bila ingin menekan biaya tahap awal: **mulai dari A saja (Rp 393.000)** — cukup untuk membangun 3 node + gateway dan membuktikan telemetri LoRa end-to-end. Testbed (B) bisa menyusul setelah telemetri terbukti.

---

## 5. KEPUTUSAN DESAIN YANG HEMAT BIAYA

### 5.1 GPS Neo-6M DIHILANGKAN dari prototipe (hemat ±Rp 144.000)

Dokumen Tahap 2 mencantumkan GPS Neo-6M per node untuk timestamping. **Prototipe tidak memakainya**, dan ini justru lebih baik secara teknis:

- Lag time hulu→hilir memerlukan **satu basis waktu yang sama** untuk semua node.
- GPS per node = 3 klok independen, masing-masing punya *clock skew* dan galat akuisisi UTC puluhan milidetik s.d. ratusan milidetik, yang harus dikoreksi — ini sumber galat tambahan, bukan pengurang.
- Gateway sudah terhubung WiFi → **sinkronisasi NTP** lalu **timestamping terpusat saat paket diterima** menghilangkan variabel clock skew antar-node secara total. Semua selisih waktu dihitung pada satu klok.
- Biaya: Rp 0 (NTP) vs Rp 144.000 (GPS), dan menghapus satu modul yang harus diintegrasikan.

**Posisi di dokumen final:** GPS tetap ditulis, tetapi sebagai **rencana pengembangan untuk deployment lapangan tanpa koneksi gateway** (skenario node jauh di luar jangkauan WiFi), bukan sebagai komponen prototipe. Ini jujur, dan menunjukkan kita paham *kenapa*, bukan sekadar mencoret komponen.

### 5.2 JANGAN meminjam gateway PALD

Gateway PALD akan dipakai untuk demo PALD sendiri. Meminjamnya berarti: (a) dua proyek tidak bisa demo bersamaan, (b) firmware harus di-flash bolak-balik sehingga risiko lupa flash-balik sangat tinggi (di PALD sudah ada `DEMO_NO_SLEEP` yang wajib dikembalikan ke 0 — menambah satu titik kegagalan lagi). Harga satu modul LoRa (Rp 42.000) jauh lebih murah daripada risiko alat lain gagal di hari-H.

---

## 6. CATATAN VERIFIKASI

- Harga di atas adalah estimasi berdasarkan BOM dokumen Tahap 2 + harga pasar umum. **Verifikasi saat checkout** sebelum dipakai di dokumen final.
- Yang harus dibeli lebih dulu dan jadi **critical path**: item #1 (LoRa Ra-02 ×3) dan #2 (antena ×4). Lead time pengiriman modul RF dari marketplace bisa 3–7 hari; jadi pesan hari ini agar tidak menahan seluruh jadwal.
- Modul LoRa Ra-02 harus **433 MHz** (bukan 868/915 MHz). Antena wajib terpasang saat transmit — menyalakan LoRa tanpa antena bisa merusak PA-nya.
