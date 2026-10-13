# PoC-3 — HASIL END-TO-END MINI (Sensor → Radio → Gateway)

Tanggal uji: 19 Sep 2026
- Node: `poc3_node` · ESP32-S3 #1 · JSN (3.3V, median/5) + Ra-02 FGV (TX)
- Gateway: `poc3_gateway` v2 · ESP32-S3 #2 + Ra-02 PALD (RX) + OLED SSD1306
- Paket 5 byte: `node_id | seq | level_mm (0xFFFF=INVALID) | crc8`
- Radio: 433.0 MHz, SF9, BW125, CR4/5, sync 0xF1, CRC on (config final)

## Data terukur

```
seq 0–8   : level=INVALID (sensor sedang dipegang/disetting)  -> aturan
            "unknown ≠ 0" terbukti bekerja end-to-end di gateway
seq 9–15  : transisi 625 -> 498 -> 477 -> 463 mm (reposisi sensor,
            median menelan fluktuasi tanpa lonjakan palsu)
seq 16–20 : 460, 465, 460, 460, 460, 460 mm -> settle, jitter 0 mm
SUMMARY   : ok=19 crc_bad=0 foreign=0 lost=0  PDR=100.0%
RSSI -65..-86 dBm (posisi node berbeda dari PoC-2), SNR 9.3–13.0 dB
```

## Kriteria lulus vs hasil

| Kriteria | Syarat | Terukur | Vonis |
| :------- | :----- | :------ | :---- |
| level_mm gateway ≡ node | CRC tak pernah gagal | 0 gagal, PDR 100% | ✅ |
| INVALID tampil di kedua sisi | bukan 0 | 9 siklus INVALID | ✅ |
| PDR 5 menit | ≥ 99% | 100% | ✅ |
| Jitter level statis | kecil | 0 mm pada settle | ✅ |

## Vonis

**PoC-3 LULUS — ladder PoC (1: sensor, 2: radio, 3: end-to-end) TUNTAS.**
Sistem minimum FGV hidup dan terukur: sensor → median → CRC → LoRa →
gateway (+ OLED, tampil INVALID/angka benar). Kerangka ini adalah embrio
langsung firmware final; yang tersisa adalah pembesaran, bukan pembuktian.

## Catatan kecil

- RSSI lebih rendah dari PoC-2 (−73 vs −28 dBm): posisi/antena berbeda.
  SNR tetap 9–13 dB → kualitas link aman. Uji jarak jauh opsional.
- Transisi seq 9–15 menegaskan fungsi median: fluktuasi reposisi tidak
  bocor jadi data palsu.
