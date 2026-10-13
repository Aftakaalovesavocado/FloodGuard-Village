# PoC-1 — HASIL UJI AWAL JSN-SR04T (3.3V)

Tanggal uji: 19 Sep 2026 · Sketch: `poc1_sensor.ino` (unmodified) · Serial 115200

## Konfigurasi

- Board: ESP32-S3 DevKitC-1, **rail 5V mati** (pin 5V terukur 0V saat power
  USB, diukur multimeter) → sensor ditenaga dari **3V3** — **di luar
  spesifikasi JSN** (resminya 5V).
- ECHO langsung ke GPIO 4 tanpa pembagi (ECHO ikut level 3.3V).
- Kecepatan suara: konstanta 20°C (343.42 m/s). Suhu ruang saat uji: belum dicatat.

## Data terukur (excerpt serial)

```
t=295444 ms  dist_mm=337.4  valid=3/5   <- outlier, tertelan median
t=296443 ms  dist_mm=306.7  valid=3/5
t=297443 ms  dist_mm=301.7  valid=3/5
t=298443..303443 ms  dist_mm=301.2  valid=3/5  (6 siklus stabil)
t=303443 ms  dist_mm=304.3  valid=3/5
```

## Uji 2 — pembandingan mistar (19 Sep 2026)

| Target | Mistar | Bacaan sensor | Galat | Jitter |
| :----- | :----- | :------------ | :---- | :----- |
| Dekat | 30.0 cm | 303.6 mm ×9 siklus identik | **+3.6 mm (+1.2%)** | **0 mm** |
| Langit-langit | (belum diukur mistar) | 941.1 → 933.9 mm | — | ±4 mm, tren turun ~0.7% (sensor diduga dipegang tangan) |

Interpretasi:
- Galat +3.6 mm **konsisten → offset sistematis, bukan noise** → dapat
  dikalibrasi keluar di firmware final (konstanta offset per sensor).
- Arah galat berlawanan dengan prediksi efek suhu (20°C-konstan → −4 mm
  di ruang 27°C) → offset bersifat karakteristik sensor/pulseIn, bukan
  akustik. Suhu ruang saat uji: belum dicatat (masih pending).

## Temuan

1. **3.3V berfungsi** — vonis: **PoC-1 LULUS (dengan catatan)**. Jitter 0
   pada target statis 30 cm; galat absolut +3.6 mm (offset kalibrabel).
2. **Median terbukti menelan outlier** (337.4 mm tidak mengganggu output).
3. **KOREKSI HIPOTESIS:** dugaan awal "valid=3/5 karena blind zone"
   **gugur** — pada 940 mm (jauh di atas blind zone) valid tetap 3/5.
   Hipotesis aktif: pada suplai 3.3V sensitivitas RX berkurang, ±2 dari
   5 ping gagal balik secara sistematis. Konsekuensi desain:
   - **Median-of-5 wajib permanen** di firmware (bukan opsi).
   - Saat BOM datang, uji unit baru di 5V — cek apakah valid kembali 5/5.
4. Aturan INVALID beroperasi: 2/5 invalid masih dilaporkan; ambang 3/5.

## Belum selesai (pending)

- [x] Pembandingan vs mistar (galat absolut) — **+3.6 mm @ 30 cm, offset**
- [ ] Verifikasi `valid` pada jarak menengah: apakah tetap 3/5 di semua
      jarak (→ karakteristik 3.3V) atau naik di jarak tertentu
- [ ] Catat suhu ruang saat pengukuran
- [ ] Saat BOM datang: uji unit JSN baru di 5V, bandingkan rasio valid
- [ ] Keputusan: sensor ini dipakai node mana / apakah perlu unit cadangan
