# PoC-3 — Wiring End-to-end (JSN + LoRa di satu board)

Board target: `esp32:esp32:esp32s3` · Serial: 115200
- `poc3_node` → ESP32-S3 **#1**: JSN (3.3V) + Ra-02 FGV (TX)
- `poc3_gateway` → ESP32-S3 **#2**: Ra-02 pinjaman PALD (RX)

> ⚠️ Antena 433 MHz terpasang di kedua modul sebelum sketch jalan.

## Node #1 — dua perangkat, satu board

| Sinyal | GPIO | Perangkat |
| :----- | :--- | :-------- |
| JSN VCC | 3V3 | Sensor (path 3.3V, tervalidasi PoC-1) |
| JSN GND | GND | |
| JSN TRIG | GPIO 5 | |
| JSN ECHO | GPIO 4 | langsung (ECHO ikut 3.3V) |
| LoRa VCC | 3V3 | Ra-02 3.3V only |
| LoRa GND | GND | |
| LoRa NSS/SCK/MISO/MOSI/RST/DIO0 | 10 / 12 / 13 / 11 / 9 / 8 | |

Jumlah pin aman — GPIO 4/5 tak bentrok dengan blok SPI 8–13.

## Gateway #2

Sama seperti PoC-2 (pin map gateway PALD) **+ OLED SSD1306 128×64 I2C**
yang sudah terpasang di unit receiver PALD:

| OLED (unit PALD) | ESP32-S3 #2 | Catatan |
| :--------------- | :---------- | :------ |
| VCC | 3V3 | |
| GND | GND | |
| SDA | GPIO 4 | sesuai firmware PALD (receiver_gateway.ino:67-68) |
| SCL | GPIO 5 | alamat I2C 0x3C |

> ⚠️ **Catatan arsitektur:** FGV ARSITEKTUR §2.3 menulis OLED gateway di
> SDA=15/SCL=16. Unit PALD memakai 4/5 — sementara kita ikut unit PALD.
> GPIO 4/5 tidak bentrok dengan LoRa (8–13). Saat gateway FGV final
> dirakit dengan OLED sendiri, kembali ke 15/16 per dokumen.
> Firmware PALD tetap tidak disentuh.

## Uji end-to-end

1. Flash node → Serial #1: `[1] seq=N level_mm=XXX TX=OK`
2. Flash gateway → Serial #2: `NODE 1 seq=N level_mm=XXX RSSI=... SNR=...`
3. **Uji konsistensi angka**: level_mm di gateway harus = level_mm di node
   (CRC menjamin byte-nya identik).
4. **Uji INVALID**: tangkap/tutup sensor → node kirim `level=INVALID`
   (0xFFFF), gateway menampilkan `level=INVALID` — bukan 0.
5. **Uji jarak**: pisahkan node & gateway beberapa meter, ulangi.
6. Bench 5 menit → catat `SUMMARY ... PDR=...%`.

## Kriteria lulus PoC-3

- level_mm gateway ≡ level_mm node (CRC tidak pernah gagal, 10 menit).
- Paket INVALID tampil sebagai INVALID di kedua sisi, bukan 0.
- PDR ≥ 99% dalam 5 menit pada jarak normal meja.
