# PoC-1 — Wiring JSN-SR04T + ESP32-S3 DevKitC-1

Board target: `esp32:esp32:esp32s3` · Sketch: `poc1_sensor.ino` · Serial: 115200

## Tabel wiring

> **STATUS AKTUAL (19 Sep 2026):** pin 5V (VIN) board S3 ini terukur 0V
> saat power dari USB (diukur multimeter) — jalur USB→5V dianggap putus.
> **Jalur yang dipakai: 3V3**, di luar spesifikasi JSN (resminya 5V).
> Konsekuensi: ECHO ikut 3.3V, pembagi tegangan tidak wajib. Diverifikasi
> dulu lewat pengukuran vs mistar (30–80 cm); bila tidak akurat, kembali
> ke 5V eksternal + pembagi (lihat Fallback di bawah).

### Jalur 3V3 (AKTIF)

| JSN-SR04T | Sambungkan ke | Catatan |
| :-------- | :------------ | :------ |
| VCC (merah) | 3V3 | Di luar spesifikasi — divalidasi vs mistar |
| GND (hitam) | GND | |
| TRIG (kuning) | GPIO 5 | Trigger 3.3V — aman langsung |
| ECHO (putih) | GPIO 4 **langsung** | ECHO ~3.3V, pembagi tidak wajib |

### Jalur 5V (FALLBACK — bila 3V3 terbukti kurang)

| JSN-SR04T | Sambungkan ke | Catatan |
| :-------- | :------------ | :------ |
| VCC (merah) | 5V eksternal (charger/power bank) | Pin 5V board rusak |
| GND (hitam) | GND — **wajib digabung GND board** | Common ground |
| TRIG (kuning) | GPIO 5 | Aman langsung |
| ECHO (putih) | GPIO 4 **via pembagi 1k/2k** | ECHO 5V → 3.33 V |

## Rangkaian pembagi tegangan ECHO (fallback 5V saja)

```
JSN ECHO (5V) ──[ R1 1kΩ ]──┬── GPIO 4
                            │
                         [ R2 2kΩ ]
                            │
                           GND
```

Jangan pernah sambungkan ECHO 5V langsung ke GPIO — di luar spesifikasi 3.3V.

## Checklist sebelum flash

1. Semua kabel sesuai tabel di atas; ECHO lewat pembagi.
2. Board terdeteksi (cek port di IDE / `arduino-cli board list`).
3. Sketch compile bersih (sudah diverifikasi compile via arduino-cli).

## Baca output serial

- `dist_mm=XXX.X  valid=5/5` → normal, median dari 5 ping valid.
- `INVALID (n/5 ping invalid)` → sensor tidak terbaca: cek wiring, blind
  zone (objek < 25 cm), atau muka air di luar window 200–4500 mm.
- Nilai INVALID **tidak pernah** ditampilkan sebagai angka 0 — by design.

## Cara uji cepat (tanpa protokol 3×10 dulu)

1. Arahkan sensor ke dinding/lantai, ukur jarak nyata pakai mistar.
2. Bandingkan dengan `dist_mm` di serial. Galat konsisten = offset
   kalibrasi (catat suhu ruang saat pengukuran).
3. Tangannya di depan sensor → harus langsung `INVALID`, bukan angka 0.
