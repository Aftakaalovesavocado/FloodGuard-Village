# PoC-2 — Wiring Radio (2× ESP32-S3 + 2× LoRa Ra-02 SX1278 433 MHz)

Board target: `esp32:esp32:esp32s3` · Serial: 115200
- `poc2_tx_node` → ESP32-S3 **#1** + Ra-02 milik FGV (TX counter 1 paket/detik)
- `poc2_rx_gateway` → ESP32-S3 **#2** + Ra-02 **pinjaman PALD** (RX kontinu)

> ⚠️ **ATURAN EMAS: ANTENA TERPASANG SEBELUM SKETCH TX JALAN.**
> TX tanpa antena merusak PA SX1278 dalam hitungan detik — permanen.
> Pasang antena 433 MHz (bukan 868/915!) di KEDUA modul.

## Wiring — kedua board identik

Pin map = gateway PALD (`Code ESP/receiver_gateway.ino` baris 59–64),
yang identik dengan pin map FGV `ARSITEKTUR_DAN_PROTOKOL.md` par.2.1:

| Ra-02 | ESP32-S3 | Catatan |
| :---- | :------- | :------ |
| VCC | **3V3** | JANGAN 5V — Ra-02 3.3V only |
| GND | GND | |
| NSS (CS) | GPIO 10 | |
| SCK | GPIO 12 | |
| MISO | GPIO 13 | |
| MOSI | GPIO 11 | |
| RST | GPIO 9 | |
| DIO0 | GPIO 8 | interrupt paket |

## Checklist sebelum flash

1. Antena 433 MHz terpasang di kedua modul. (Serius. Cek lagi.)
2. VCC ke 3V3, bukan 5V / VIN.
3. Kedua sketch sudah diverifikasi compile `esp32:esp32:esp32s3`.
4. Disiplin pinjam PALD: firmware PALD **tidak disentuh**; modul dipinjam
   sebagai komponen mentah + antena; catat & kembalikan di hari yang sama.
5. Kalau PALD dipakai demo bersamaan di lokasi, jangan lupa sync word
   kita (0xF1) beda dari PALD — sengaja, biar tidak saling mendengar.

## Prosedur bench test

1. Flash TX ke board #1, RX ke board #2. Buka 2 jendela Serial Monitor
   (atau 2 port — setiap board port USB sendiri).
2. Jarak 2 m indoor, bebas halangan → tunggu `RX seq=...` mengalir.
3. Jalankan **5 menit**, catat SUMMARY PDR di sisi RX.
4. Ulangi di ±10 m / lewat satu dinding, catat ulang.
5. (Opsional) Tekan reset TX — RX harus melihat lompatan seq → `lost`
   naik. Itu bukti deteksi paket hilang berfungsi.

## Kriteria lulus PoC-2

- PDR ≥ 99% dalam 5 menit pada 2 m.
- RSSI/SNR terekam sebagai baseline radio indoor.
- CRC gagal (bila dipaksakan) dibuang, tidak pernah dipakai sebagai data.

## Baca output serial (RX)

- `RX seq=N RSSI=-xx dBm SNR=xx.x dB` → paket valid.
- `RX BROL (magic/crc gagal)` → data rusak dibuang — by design, bukan bug.
- `SUMMARY ok=... PDR=...%` tiap 30 detik → angka untuk HASIL_POC2.
