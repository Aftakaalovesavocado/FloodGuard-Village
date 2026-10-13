# FGV PROTOTIPE — RENCANA KERJA & JADWAL TAHAP 3 (FINAL)

Disusun 16 Sep 2026 · Status: **Tahap 3 (Final)** — output akhir: **PROTOTIPE & MAKALAH**

> ⚠️ Panitia belum mengirimkan panduan/undangan Tahap 3. Jadwal di bawah disusun berbasis timeline panduan Tahap 2 (presentasi akhir ∈ Minggu Kedua Sept, awarding 29–30 Okt 2026) dan **harus dikalibrasi ulang** begitu dokumen resmi turun.

---

## 1. APA YANG DINILAI DI FINAL (dari panduan resmi)

| # | Variabel | Bobot | Implikasi ke kerja kita |
| :--- | :--- | ---: | :--- |
| 1 | Kreativitas & Orisinalitas Ide | 15% | Sudah kuat di dokumen |
| 2 | Fokus Masalah | 10% | Sudah kuat |
| 3 | **Fungsi & kegunaan — "karya telah berfungsi (bukan hanya ide)"** | **15%** | **← ini target utama kita: prototipe harus benar-benar jalan** |
| 4 | **Inovasi dan/atau implementasi (nilai tambah, peluang bisnis, sudah diimplementasikan)** | **20%** | **← butuh bukti implementasi nyata + data uji** |
| 5 | Penulisan Ringkasan | 10% | Revisi angka hasil ukur |
| 6 | Keberlanjutan SDGs (DEI 15% + Quality Education/SDGs 15%) | 30% | Sudah kuat, pertahankan |

**Kesimpulan strategis: 35% skor bergantung pada "alatnya nyata dan jalan".** Prioritas kerja bukan menambah fitur, tapi membuat yang sudah dijanjikan **benar-benar terukur**.

---

## 2. JADWAL KERJA (target: siap demo dalam 3–4 minggu)

### FASE 0 — PENGADAAN (Hari 1–2) ⚡ CRITICAL PATH
- [ ] Pesan item Prioritas A (`BOM_PENGADAAN.md`): LoRa Ra-02 ×3, antena ×4, JSN-SR04T ×2, board ×2, resistor pembagi
- [ ] Sambil menunggu: kerjakan Fase 1 & 2 (tidak butuh hardware)

### FASE 1 — VALIDASI LOGIKA INFERENSI (Hari 1–3) — *bisa dikerjakan tanpa hardware*
- [x] Audit integrasi BMKG → **menemukan bug `tp` (fungsi hujan mati)**
- [x] Re-kalibrasi 3 membership function berbasis 140 sampel BMKG nyata
- [ ] Validasi 27 rule Layer 2 + seluruh skenario di simulator Python
- [ ] Hasil: `sim/HASIL_VALIDASI.md` — jadi bukti uji yang bisa dilampirkan

### FASE 2 — FIRMWARE (Hari 3–8) — *sebagian tanpa hardware*
- [ ] `shared_fgv.h` — pin map, konstanta, struct paket, CRC8, MF (satu sumber kebenaran)
- [ ] `fuzzy_layer1.h` — MF v2 + 27 rule Sugeno + kategorisasi level
- [ ] `flow_manning.h` — geometri saluran + Manning + fallback Doppler
- [ ] `node_fgv.ino` — baca sensor (median filter 5) → fuzzy → paket LoRa
- [ ] `pattern_layer2.h` — classifier 27 baris + konfirmasi backwater
- [ ] `gateway_fgv.ino` — RX kontinu + NTP + BMKG + L2 + HTTP POST + alert
- [ ] **Kompilasi bersih via `arduino-cli`** (verifikasi nyata sebelum hardware datang)
- [ ] Adaptasi `flood-dashboard-server` → tampilkan 3 node + kelas pola ancaman

### FASE 3 — RAKIT & UJI TELEMETRI (Hari 8–12)
- [ ] Rakit 1 node + gateway di breadboard → uji LoRa point-to-point (RSSI/SNR/PDR)
- [ ] Replikasi ke 3 node
- [ ] Uji per node: kalibrasi JSN-SR04T vs mistar (50 pengulangan, laporkan galat sebenarnya)
- [ ] Uji pembagi tegangan ECHO (perbaikan dari MVP)

### FASE 4 — TESTBED CLOSED-LOOP (Hari 12–18)
- [ ] Rakit talang 3 segmen + pompa + reservoir sirkulasi
- [ ] Ukur geometri & kemiringan tiap segmen → isi parameter Manning
- [ ] Validasi Manning vs debit volumetrik (gelas ukur + stopwatch) & FS400A
- [ ] Skenario uji terekam datanya:
  - S1 Normal (baseline) — untuk `Q_ref` dan `level` dasar
  - S2 Hujan lebat tanpa kenaikan air → **uji leading indicator (lead time)**
  - S3 Banjir kiriman: naikkan debit pompa dari sisi hulu, amati rambatan
  - S4 Sumbatan: sekat di hilir, amati hilir naik + debit turun → kelas BACKWATER
  - S5 Lokal: siram hanya segmen tengah
  - S6 Kritis serentak: semua segmen naik
- [ ] Catat: waktu alarm per node, kelas pola yang keluar, lag time hulu→hilir terukur

### FASE 5 — DATA & DOKUMEN (Hari 18–24)
- [ ] Tabel hasil uji nyata (ganti semua klaim tanpa bukti)
- [ ] Revisi makalah & rancang bangun → versi FINAL
- [ ] Deck presentasi + video demo (rekam alat benar-benar jalan)
- [ ] Dry-run tanya-jawab juri (lihat §4)

---

## 3. RISIKO & MITIGASI

| Risiko | Dampak | Mitigasi |
| :--- | :--- | :--- |
| Modul LoRa lama datang | Seluruh jadwal geser | **Pesan hari ini.** Kerjakan Fase 1–2 paralel |
| Board node berbeda tipe (S3 + V1) | Bug pin map | Pin map via makro board, kompilasi di 2 target sebelum hardware |
| **CDM324 tidak bisa diandalkan** | Demo debit gagal | Manning jadi jalur utama; CDM324 hanya bench test terpisah. **Demo tidak boleh bergantung padanya** |
| Testbed tidak mencapai kecepatan aliran realistis | MF debit mati (bug sama seperti hujan) | Pakai **Indeks Debit Q/Q_ref**, bukan kecepatan absolut |
| JSN-SR04T ECHO 5V merusak GPIO | Node mati | Pembagi 1k/2k di ketiga node |
| Perhitungan waktu luap tidak stabil (tren berisik) | Angka melompat-lompat | Ganti endpoint-difference → **regresi linear least-squares** + minimal 3 sampel + deadband |
| Klaim lama di dokumen tidak didukung data | Pertanyaan juri | Semua angka di dokumen final wajib punya jejak ke pengukuran/simulasi |

---

## 4. DAFTAR PERTANYAAN JURI YANG HARUS KITA SIAPKAN JAWABANNYA

Ini bukan soal optimis — ini soal latihan. Siapkan jawaban jujur & spesifik untuk:

1. "Curah hujan BMKG itu prakiraan, bukan pengukuran. Bagaimana kalian menyebutnya *leading indicator*?"
   → *Sistem memakai prakiraan 3 jam ke depan sebagai **pemicu kewaspadaan awal**, lalu mengonfirmasi dengan pengukuran aktual (level + debit). Keduanya tidak dicampur: prakiraan menaikkan kesiapan, pengukuran yang memicu alarm akhir.*
2. "Akurasi 85% itu terhadap apa? Bagaimana mengukurnya?"
   → *Definisikan metrik presisi: dari N peringatan SIAGA/BAHAYA yang dipancarkan, berapa yang benar-benar diikuti kenaikan air di atas ambang (true positive rate), dan berapa kejadian luapan yang terlewat (false negative). Instrumen: level terukur node + pencatatan manual.*
3. "Kenapa tidak pakai satu sensor LIDAR/radar saja yang lebih akurat?"
   → *Biaya per titik, ketahanan lapangan, dan kebutuhan komunitas desa merawat sendiri.*
4. "Apa yang terjadi kalau LoRa-nya putus?"
   → *Tabel kegagalan di `ARSITEKTUR_DAN_PROTOKOL.md` §4. Sistem menandai node OFFLINE dan menyatakan keterbatasan datanya, bukan berpura-pura aman.*
5. "Bagian mana yang kalian rancang sendiri vs paket yang sudah ada?"
   → *Jujur: ESP32 & LoRa itu modul siap pakai; yang kami rancang adalah layer inferensi 54 rule, normalisasi input berbasis baseline, pattern classifier spasial, dan skema estimasi debit adaptif.*
6. "Kenapa GPS tidak ada di prototipe?" (bila ditanya setelah melihat alat)
   → *Lihat `BOM_PENGADAAN.md` §5.1 — timestamping terpusat menghilangkan clock skew. GPS direncanakan untuk deployment tanpa gateway.*

---

## 5. ATURAN KERJA DOKUMEN (supaya tidak terulang)

1. **Tidak ada angka di dokumen tanpa sumber.** Setiap angka = hasil ukur (tanggal & metode) atau hasil simulasi (skrip yang bisa dijalankan ulang).
2. **Bedakan "dirancang / disimulasikan / diukur".** Kata "berhasil", "tervalidasi", "tercapai" hanya untuk yang benar-benar diukur.
3. **Rencana boleh ditulis sebagai rencana.** Jangan ditulis sebagai capaian.
