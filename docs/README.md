# FGV PROTOTIPE — TAHAP 3 (FINAL) SOBAT COMPETITION 2026

Folder ini adalah **ruang kerja perancangan prototipe** untuk Tahap 3/Final.
Mulai: 16 Sep 2026.

## STATUS SEKARANG

| Fase | Status |
| :--- | :--- |
| Fase 0 — Pengadaan | ⬜ belum dipesan (daftar di `BOM_PENGADAAN.md`) |
| Fase 1 — Validasi logika inferensi | ✅ **SELESAI** → `sim/HASIL_VALIDASI.md` |
| Fase 2 — Firmware | ⬜ belum |
| Fase 3 — Rakit & uji telemetri | ⬜ menunggu komponen |
| Fase 4 — Testbed closed-loop | ⬜ |
| Fase 5 — Data & dokumen final | ⬜ |

## BACA DULU (urutan untuk konteks)

1. **`PLAN_ROADMAP.md`** — rencana kerja, risiko, dan daftar pertanyaan juri yang harus disiapkan.
2. **`KALIBRASI_BMKG.md`** — temuan bug integrasi BMKG (input hujan mati) + MF pengganti terkalibrasi. **Ini temuan paling penting.**
3. **`ARSITEKTUR_DAN_PROTOKOL.md`** — arsitektur, pin map, protokol paket LoRa 16 byte, tabel Layer 2. **Sumber kebenaran firmware.**
4. **`DESAIN_TESTBED.md`** — rancangan rig uji hidrolika + prosedur pengujian S1–S6.
5. **`BOM_PENGADAAN.md`** — daftar belanja + keputusan hemat biaya (GPS dihapus, dll).

## KODE

| File | Isi |
| :--- | :--- |
| `sim/fgv_engine.py` | Referensi logika inferensi (Layer 1 fuzzy + Layer 2 classifier + Manning + CRC8). **Firmware C++ harus menghasilkan angka yang identik.** |
| `sim/run_validasi.py` | Menjalankan seluruh validasi model, termasuk bukti bug & tabel 27 kombinasi |
| `sim/HASIL_VALIDASI.md` | Keluaran validasi (angka siap dikutip ke makalah) |
| `sim/simulasi_testbed.html` | Hal. 1 — simulator rig testbed 1-saluran (bentuk mengikuti foto rig fisik). Engine = port JS `fgv_engine.py` v2, self-test 38 vektor di halaman. Hidrolika first-order (visualisasi, bukan Manning). |
| `sim/simulasi_multinode.html` | Hal. 2 — simulator 3 simpul hulu-tengah-hilir + Layer 2 pattern classifier (27 kombinasi, tabel interaktif). Dinamika tiap skenario S1–S6 telah diuji di Python & Node sebelum halaman ditulis. Klaim: **disimulasikan**. |

Menjalankan ulang: `cd sim && python run_validasi.py`

## TIGA HAL YANG BERUBAH DARI DOKUMEN TAHAP 2

| # | Yang berubah | Alasan |
| :--- | :--- | :--- |
| 1 | MF curah hujan: domain 10–60 mm/hari → **0–10 mm per 3 jam** | MF lama tidak pernah aktif (input BMKG nyata hanya 0–4 mm) → variabel hujan mati |
| 2 | Input debit: L/menit absolut → **Indeks Debit I_Q = Q/Q_ref** | Portabel antara skala lab dan lapangan; sensor-agnostik |
| 3 | Ditambah **lapisan keselamatan** (safety floor) | Risiko jenuh: saluran penuh 100% hanya menghasilkan risk 40 sebelum ditambal |

## ATURAN YANG TIDAK BOLEH DILANGGAR

1. **Setiap angka di dokumen final harus punya jejak** ke pengukuran (dengan tanggal & metode) atau simulasi (skrip yang bisa dijalankan ulang).
2. **Bedakan tegas**: "dirancang" / "disimulasikan" / "diukur". Kata "berhasil", "tervalidasi", "tercapai" hanya untuk yang benar-benar diukur.
3. **Rencana ditulis sebagai rencana**, tidak ditulis sebagai capaian.
