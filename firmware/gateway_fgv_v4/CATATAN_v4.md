# gateway_fgv_v4 — Port ke receiver baru (desain HMI PALD)

Status: **COMPILE OK (belum diuji di hardware)** · 6 Okt 2026
Di-port dari `gateway_fgv_v3` → folder ini. `gateway_fgv_v3` tetap utuh (masih bisa di-flash ke unit OLED lama).

## Bukti kompilasi (arduino-cli, FQBN `esp32:esp32:esp32s3`)

```
Sketch uses 1079097 bytes (82%) of program storage space. Maximum is 1310720 bytes.
Global variables use 49208 bytes (15%) of dynamic memory, leaving 278472 bytes for local variables.
```

Exit code 0 (bersih). Pembanding: receiver PALD = 87% flash / 16% RAM.

## Yang berubah dari v3

| Bagian | v3 (lama) | v4 (receiver baru) |
| :--- | :--- | :--- |
| Display | OLED SSD1306 128x64 I2C (SDA 4 / SCL 5) | TFT ST7735S 160x128 SPI (CS 6 / DC 7 / RST 4 / BLK 5; SCK 12 / MOSI 11 share LoRa) |
| Tombol | — | NEXT=1 / BACK=2 (sisi lain ke GND, INPUT_PULLUP) |
| Buzzer | — | ALARM=14 (bebek 250 ms saat BAHAYA), STATUS=15 (chirp), via transistor NPN |
| UI | 1 layar teks monokrom | 5 halaman auto-rotate 8 dtk: DASHBOARD / NODE / POLA L2 / HUJAN / SISTEM + boot checklist + bingkai merah kedip saat BAHAYA |

Wiring tombol/buzzer mengikuti: `PALD Code ESP/receiver_gateway/WIRING_BUTTONS_BUZZERS.md`.

## Yang SENGAJA tidak berubah (dipertahankan 1:1 dari v3)

Radio (433 MHz, SF9, BW125, CR5, **sync 0xF1** — beda dari PALD biar tidak saling baca, CRC on) ·
paket 18 B + CRC (skip offset 9) · fuzzy L1 · pattern L2 · BMKG · Firebase push · simulasi hujan.

Dibuktikan via region-equality check saat porting: blok JSON builder (`postToDashboard`),
blok BMKG/sim, dan `wifiMaintain` **byte-identik** dengan v3.

## Checklist uji setelah flash (isi saat pengujian nyata)

- [ ] Boot screen: 4 baris checklist (LoRa / WiFi / BMKG / Firebase) — OK/FAIL
- [ ] 5 halaman tampil + auto-rotate 8 dtk
- [ ] Tombol NEXT/BACK pindah halaman + bunyi klik halus
- [ ] Warna TFT normal (kalau kebalik → set `tft.invertDisplay(true)` di setup)
- [ ] Buzzer BAHAYA: naikkan sim hujan di dashboard → bebek 250 ms + border kedip merah
- [ ] Paket node masuk (serial `NODE x seq=...` + chip N1/N2/N3 hidup)

## Catatan

- Tombol & buzzer **opsional** di level firmware: kalau belum terpasang, firmware tetap jalan
  (tombol pull-up, buzzer diam). TFT = wajib.
- Kalau unit receiver ini bekas unit PALD: flash v4 menggantikan firmware PALD di unit itu.
  Flash balik kapan saja dari `PALD Code ESP/receiver_gateway/receiver_gateway.ino`.
