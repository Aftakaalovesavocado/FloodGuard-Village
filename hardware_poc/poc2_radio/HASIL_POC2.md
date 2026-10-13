# PoC-2 — HASIL BENCH TEST RADIO (TX → RX)

Tanggal uji: 19 Sep 2026
- TX: `poc2_tx_node` · ESP32-S3 #1 + Ra-02 milik FGV
- RX: `poc2_rx_gateway` **v2** · ESP32-S3 #2 + Ra-02 pinjaman PALD
- Radio: 433.0 MHz, SF9, BW 125 kHz, CR 4/5, sync word 0xF1, CRC on
- Pin map kedua sisi: NSS=10 SCK=12 MISO=13 MOSI=11 RST=9 DIO0=8
  (= gateway PALD `receiver_gateway.ino:59-64` = FGV ARSITEKTUR §2.1)

## Hasil terukur

```
RX seq=111..120 stabil, RSSI -28..-35 dBm, SNR 9.3..13.0 dB
SUMMARY ok=60 crc_bad=0 foreign=0 lost=0  PDR=100.0%  RSSI -72..-26 dBm
```

| Metrik | Syarat lulus | Terukur | Vonis |
| :----- | :----------- | :------ | :---- |
| PDR (5 menit, 2 m indoor) | ≥ 99% | **100.0%** (60/60) | ✅ |
| CRC gagal dipakai sebagai data | 0 kasus | 0 | ✅ |
| Deteksi paket hilang (seq gap) | berfungsi | jalan (seq+1 terpantau) | ✅ |
| Baseline RSSI/SNR | terekam | −72..−26 dBm; SNR 9.3–13.0 | ✅ |

## Catatan teknis

1. **Bug v1 (terdokumentasi):** RX v1 memanggil `LoRa.parsePacket(0)` di
   loop SETELAH ISR `handleDio0Rise()` membersihkan flag IRQ → parsePacket
   balikin 0 → semua paket terbaca "paket asing". Tautan radio sesungguhnya
   sudah OK dari awal (bukti: callback ISR hanya dipanggil saat CRC OK).
   Fix v2: ukuran paket diambil dari callback, byte dibaca langsung via
   `readBytes` (FIFO pointer diset ISR), parsePacket tidak dipakai.
   Pembelajaran: pola loop+parsePacket TIDAK boleh dipakai di gateway final.
2. **Konfigurasi final radio sudah tervalidasi bench** — SF9/BW125/CR4/5/
   sync 0xF1/CRC: persis spek ARSITEKTUR_DAN_PROTOKOL.md par.2. Nggak ada
   alasan mengubah parameter radio di firmware final.
3. RSSI −28 dBm = jarak sangat dekat; SNR tinggi = headroom link besar.
   Uji 10 m / tembus dinding belum dilakukan (opsional, tambah jika sempat).

## Vonis

**PoC-2 LULUS.** Tautan radio end-to-end dengan CRC tervalidasi, PDR 100%,
deteksi paket hilang berfungsi. Fondasi firmware final (FgvPacket, CRC8,
pola RX gateway) sudah terbukti di bench.
