# FGV PROTOTIPE — ARSITEKTUR SISTEM, PIN MAP & PROTOKOL KOMUNIKASI

Versi: 2.0 (prototipe Tahap 3) · 16 Sep 2026
**Ini dokumen sumber kebenaran untuk firmware.** Kalau kode dan dokumen ini berbeda, dokumen ini yang benar.

---

## 1. ARSITEKTUR

```
┌──────────────────────────┐   LoRa 433 MHz    ┌───────────────────────────────┐
│ NODE 1 — HULU            │  ──────────────▶  │ CENTRAL GATEWAY (Balai Desa)  │
│ ESP32 + JSN-SR04T        │                   │ ESP32-S3 + LoRa RX kontinu    │
│ Manning → I_Q → Fuzzy L1 │                   │  ├ NTP sync (WiFi)            │
└──────────────────────────┘                   │  ├ Fetch BMKG (tp, mm/3jam)   │
┌──────────────────────────┐                   │  ├ Timestamp terpusat         │
│ NODE 2 — TENGAH          │  ──────────────▶  │  ├ Layer 2 Pattern Classifier │
│ sama                     │                   │  └ Alert Engine + Dashboard   │
└──────────────────────────┘                   └───────────────┬───────────────┘
┌──────────────────────────┐                                   │
│ NODE 3 — HILIR           │  ──────────────▶                  │ HTTP POST JSON
│ sama                     │                                   ▼
└──────────────────────────┘                    ┌──────────────────────────┐
                                                │ Cloud: Sungai Watch      │
                                                │ + WhatsApp / Telegram    │
                                                └──────────────────────────┘
```

**Prinsip pembagian kerja (warisan dari MVP, dipertahankan):**
- **Layer 1 (Fuzzy Sugeno, 27 rule)** jalan **di tiap node** → mengompresi 3 variabel jadi 1 skalar `risk 0–100`. Ini yang membuat paket LoRa tetap 20 byte dan hemat daya.
- **Layer 2 (Spatial Pattern Classifier)** jalan **di gateway** → butuh data tiga node sekaligus, tidak mungkin di node.
- Total **54 rule** (27 L1 × independen di 3 node + 27 L2). Angka ini konsisten dengan dokumen Tahap 2.

---

## 2. PIN MAP

Dua tipe board dipakai. Pin didefinisikan lewat makro board, **jangan hard-code** di fungsi.

### 2.1 Node — ESP32-S3 DevKitC-1 (Node 1 & 2)

| Fungsi | GPIO | Catatan |
| :--- | ---: | :--- |
| LoRa SCK | 12 | SPI2/FSPI default S3 |
| LoRa MISO | 13 | |
| LoRa MOSI | 11 | |
| LoRa NSS (CS) | 10 | |
| LoRa RST | 9 | |
| LoRa DIO0 | 8 | Interrupt |
| JSN-SR04T TRIG | 5 | Output 3.3V, aman |
| JSN-SR04T ECHO | 4 | **WAJIB lewat pembagi 1k/2k** (level 5V) |
| FS400A flow (opsional, ground truth) | 6 | Interrupt-capable |
| CDM324 IF envelope (opsional) | 7 | ADC1_CH6 |
| Buzzer indikator | 47 | |
| LED status | 21 | |
| Baterai sense (opsional) | 1 | ADC1_CH0, pembagi 1k/2k |

> Hindari GPIO 19/20 (USB D±), 43/44 (UART), 26–37 (flash/PSRAM internal pada varian N8R8/N16R8).

### 2.2 Node — ESP32 DevKit V1 / WROOM-32 (Node 3, eks unit MVP)

| Fungsi | GPIO |
| :--- | ---: |
| LoRa SCK | 18 |
| LoRa MISO | 19 |
| LoRa MOSI | 23 |
| LoRa NSS (CS) | 15 |
| LoRa RST | 4 |
| LoRa DIO0 | 2 |
| JSN-SR04T TRIG | 5 |
| JSN-SR04T ECHO | 34 *(input-only, tetap pakai pembagi)* |
| FS400A flow | 27 |
| Buzzer | 25 |
| LED status | 26 |

### 2.3 Gateway — ESP32-S3 (sama dengan node S3) + I2C display opsional

| Fungsi | GPIO |
| :--- | ---: |
| LoRa SCK/MISO/MOSI/NSS/RST/DIO0 | 12 / 13 / 11 / 10 / 9 / 8 |
| OLED SDA | 15 |
| OLED SCL | 16 |
| Buzzer alarm lokal | 47 |
| LED level (hijau/kuning/oranye/merah) | 21 / 38 / 39 / 40 |

**Frekuensi LoRa: 433.0 MHz. `LoRa.setSpreadingFactor(9)`, `setSignalBandwidth(125E3)`, `setCodingRate4(5)`, `setSyncWord(0xF1)` (khusus FGV, supaya tidak bentrok dengan paket PALD yang memakai sync word default). `LoRa.enableCrc()`.**
> `setSyncWord` berbeda dari PALD itu **sengaja** — kalau tidak, gateway FGV bisa mendekode paket node PALD dan sebaliknya saat dua alat ada di ruangan yang sama.

---

## 3. PROTOKOL PAKET LoRa (node → gateway)

Struct biner `__attribute__((packed))` — **20 byte**, 1 paket, tidak dipecah (FIFO SX1278 = 64 byte).

```c
struct __attribute__((packed)) FgvPacket {
  uint8_t  node_id;       // 1=HULU, 2=TENGAH, 3=HILIR
  uint8_t  seq;           // counter paket, deteksi paket hilang
  uint16_t level_mm;      // tinggi muka air dari dasar saluran (mm)
  uint16_t chan_mm;       // tinggi maksimum saluran terukur (mm) — konstanta per node
  uint16_t v_cms;         // kecepatan aliran hasil Manning / Doppler (cm/s)
  uint16_t q_lpm_x10;     // debit x10 (L/menit)
  uint8_t  risk;          // 0..100 hasil Layer 1 (Fuzzy Sugeno)
  uint8_t  level_l1;      // 0=Rendah 1=Sedang 2=Tinggi (kategorisasi risk, dihitung node)
  uint8_t  flags;         // bit0 level_ok  bit1 flow_ok  bit2 radar_ok  bit3 tto_valid  bit4 low_batt
  uint8_t  batt_pct;      // 0..100
  uint8_t  tto_menit;     // estimasi waktu meluap; 0xFF = tidak valid
  uint8_t  crc8;          // checksum sederhana data payload
};
```

Total: 1+1+2+2+2+2+1+1+1+1+1+1 = **16 byte** + header LoRa → aman, dan masih menyisakan ruang bila nanti perlu GPS (tambah 8 byte → 24 byte).

**Mengapa `chan_mm` dikirim dari node, bukan dikonfigurasi di gateway:** kalau tinggi saluran salah konfigurasi, `%` salah dan seluruh sistem salah. Menyertakannya membuat gateway bisa langsung mendeteksi mismatch konfigurasi antar-node (mismatch → flag anomali), sehingga kesalahan setup ketahuan saat perakitan, bukan saat banjir.

**Gateway timestamping:** saat paket diterima dan `seq` bertambah, gateway mencatat `millis_terima` dari klok NTP-nya. Inilah satu-satunya basis waktu yang dipakai untuk menghitung lag time hulu→hilir.

---

## 4. JALUR KOMUNIKASI & KEGAGALAN

| Kondisi | Perilaku |
| :--- | :--- |
| Node → gateway LoRa OK | Normal |
| Paket hilang (CRC gagal / seq melompat) | Gateway pakai data terakhir node tersebut, tandai `stale` di dashboard |
| Node tidak kirim > 3 siklus | Node ditandai OFFLINE; Layer 2 tetap jalan dengan node yang tersisa + peringatan keterbatasan data |
| Gateway kehilangan WiFi | Buffer data ke LittleFS (CSV), kirim ulang saat WiFi pulih; Layer 2 tetap jalan lokal |
| BMKG API gagal | Pakai nilai `tp` terakhir yang valid + tandai di dashboard. **Jangan pakai 0** (0 = "tidak hujan", itu asumsi yang menyesatkan) |
| Siklus pengiriman node | 5 detik (nilai uji; di lapangan bisa 30–60 detik untuk hemat daya) |

> **Aturan integritas data:** nilai "tidak diketahui" tidak boleh dikonversi menjadi 0. Ini kesalahan yang bikin sistem kelihatan aman padahal buta. Fallback harus eksplisit dan terlihat di dashboard.

---

## 5. PARAMETER FISIK & GEOMETRI SALURAN (input Persamaan Manning)

Setiap node diberi identitas geometris terukur:

| Parameter | Simbol | Cara peroleh |
| :--- | :--- | :--- |
| Lebar dasar saluran | b | Meteran (mm) |
| Kemiringan dinding (trapesium) | m | Ukur H:V |
| Kemiringan dasar saluran | S | Selisih elevasi / panjang segmen (waterpass) |
| Koefisien kekasaran Manning | n | Tabel standar (beton 0.013, tanah 0.025, PVC 0.010) |
| Tinggi maksimum saluran | chan_mm | Tinggi dinding |

Manning (per node, on-chip):

```
A(h) = b*h + m*h^2                        luas penampang basah
P(h) = b + 2*h*sqrt(1+m^2)                keliling basah
R(h) = A(h) / P(h)                        jari-jari hidrolis
v    = (1/n) * R(h)^(2/3) * S^(1/2)       kecepatan rata-rata
Q    = A(h) * v                           debit
```

**Verifikasi penting:** di testbed, `Q` hasil Manning dibandingkan dengan **debit terukur** (metode volumetrik: gelas ukur + stopwatch, dan/atau FS400A). Selisihnya dilaporkan apa adanya. Ini yang membuat klaim "Adaptive Flow Estimation" punya dasar, bukan hanya rumus di poster.

---

## 6. LAYER 2 — SPATIAL PATTERN CLASSIFIER

**Input:** `level_l1` tiap node (0=Rendah risk<25, 1=Sedang 25–59, 2=Tinggi ≥60) → 3³ = 27 kombinasi.
**Output:** kelas jenis ancaman + tingkat keparahan (dua hal berbeda, jangan dicampur).

Aturan (presedensi berurutan, direpresentasikan sebagai tabel 27 baris yang bisa diaudit):

```
1. semua 0                                    -> AMAN
2. hulu > tengah > hilir                      -> KIRIMAN_HULU   (gelombang turun berurutan)
3. hilir > tengah > hulu                      -> BACKWATER      (naik berurutan ke arah hilir)
4. hilir > hulu  dan  hilir >= tengah         -> BACKWATER      (hilir dominan)
5. hulu  > hilir dan  hulu  >= tengah         -> KIRIMAN_HULU   (hulu dominan)
6. tengah > hulu dan  tengah > hilir          -> LOKAL          (puncak tunggal di tengah)
7. tiga node setara levelnya                  -> SERENTAK       (kenaikan merata)
8. hanya satu node naik                       -> LOKAL
9. sisanya                                    -> CAMPURAN       (perlu verifikasi lapangan)
```

Tabel lengkap 27 baris beserta contohnya dibangkitkan dan diverifikasi di `sim/run_validasi.py` → hasil di `sim/HASIL_VALIDASI.md`.

**Pemeriksaan konfirmasi backwater (sesuai prinsip di dokumen):** jika kelas = BACKWATER, gateway mengecek apakah debit hilir **tidak naik secara proporsional** terhadap kenaikan muka air (`I_Q` hilir turun sementara `level` hilir naik). Bila ya → status dinaikkan keyakinannya menjadi *"indikasi sumbatan kuat"*. Inilah implementasi konkret dari pernyataan "hilir naik tapi debit tidak proporsional = sumbatan" yang sebelumnya hanya berupa konsep.
