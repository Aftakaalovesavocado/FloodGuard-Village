# FGV — RANCANGAN CLOSED-LOOP TESTBED "KASKADE BERTINGKAT" (v2)

> **v2 — 16 Sep 2026.** Menggantikan rancangan talang linear 3 m. Arsitektur kaskade
> bertingkat (usulan: wadah besar 1 m + 3 tingkat 30/20/10 cm) **disetujui** karena
> lebih kompak, lebih mudah dibawa ke lokasi presentasi offline, dan jatuhnya air
> antar-tingkat menghasilkan lag alami yang bagus untuk demo banjir kiriman.
> Angka-angka di dokumen ini sudah dihitung dengan `sim/fgv_engine.py`, bukan perkiraan.

---

## 0. RINGKASAN VERDICT DESAIN

| Aspek | Keputusan |
| :--- | :--- |
| Konsep kaskade bertingkat | ✅ DISETUJUI — lebih baik dari linear 3 m |
| Tingkat 30/20/10 cm | ✅ dipertahankan, dengan syarat posisi sensor (§3.1) |
| Pipa kotak per tingkat ±30 cm | ✅ dipertahankan, ukuran disesuaikan (§3.2) |
| Sensor di tengah pipa | ⚠️ DIPINDAH ke tampungan ujung (alasan fisika, §2.1) |
| JSN menghitung ketinggian | ✅ via stilling well di tampungan |
| JSN menghitung debit | ❌ JSN tidak bisa ukur debit; debit dari Manning + validasi volumetrik |
| CDM324 opsional | ✅ setuju — bench test terpisah, bukan jalur demo |

---

## 1. ARSITEKTUR SIRKULASI

```
                 [TAMPGUNAN T1]──gate──┐
                      ▲ stilling well  │ jatuh ~10 cm
 [POMPA]──valve──┐    │                 ▼
[WADAH BESAR 1m] │    │    [PIPA KOTAK T1 - 30 cm, tingkat 30 cm]
  (reservoir)    │    │
      ▲          └────┘    [TAMPGUNAN T2]──gate──┐
      │                     ▲ stilling well  │ jatuh ~10 cm
      │                     │                 ▼
      │                     │    [PIPA KOTAK T2 - 30 cm, tingkat 20 cm]
      │                     │
      │                     │    [TAMPGUNAN T3]──gate──▶ buang ke wadah
      │                     │         ▲                  (kembali ke reservoir)
      │                     │         │
      └── jalur balik ◀─────┘    [PIPA KOTAK T3 - 30 cm, tingkat 10 cm]
```

- Satu pompa, satu arah: reservoir → tingkat 30 → 20 → 10 → reservoir. **Gravity cascade.**
- Tampungan di ujung tiap pipa = **chamber ukur + basin tenang + titik kontrol** (gate).
- Node = 1 pipa + 1 tampungan + 1 sensor. Identik strukturnya dengan "segmen sungai + pos pantau".

---

## 2. TIGA JEBAKAN FISIKA (SUDAH DIHITUNG) DAN PEMECAHANNYA

### 2.1 Jebakan 1: sensor di TENGAH pipa = pembacaan hancur

- **Blind zone JSN-SR04T = 20 cm.** Sensor wajib ≥ 20 cm di atas permukaan air.
- **Sudut beam-nya lebar (±30°)** → pada jarak 20–23 cm, diameter lingkaran pantulan di air ≈ **46 cm**. Pipa kamu cuma 30 cm — beam bakal memantul ke dinding pipa dan bibir tampungan → echo palsu, median filter pun tidak bisa menolong sepenuhnya.
- Permukaan air di tengah pipa yang mengalir itu beriak + turbulen (apalagi 10 cm setelah air jatuh dari tingkat atas).

**Solusi (memindahkan posisi tanpa mengubah konsep kamu):**
1. Sensor dipasang **di atas TAMPGUNAN** (bukan tengah pipa), karena tampungan airnya dalam & tenang.
2. Tambahkan **stilling well**: pipa PVC 2" (Ø63 mm) tegak, ujung bawah tenggelam ±2 cm di air tampungan, sensor JSN duduk di atasnya.
   - Beam terpandu di dalam tube → tidak memantul dinding pipa.
   - Riak dari luar tidak masuk → permukaan datar.
   - Jarak sensor→air ±25 cm ≥ blind zone. ✔

### 2.2 Jebakan 2: RUANG VERTIKAL 10 cm antar-tingkat TIDAK CUKUP

Stilling well butuh ruang vertikal **±25 cm** di atas permukaan air (hitungan: blind zone 200 mm + kedalaman air maks ±25 mm + tube tenggelam 20 mm ≈ 245 mm).

Kalau tiga tingkat ditumpuk lurus (jarak vertikal antar-dek 10 cm), tube sensor tingkat bawah akan **menabrak dek atasnya**.

**Solusi:** susun **tangga dengan offset horizontal** — tiap tingkat maju ke depan, dan tampungan + sensor berada di **tepi luar** (di luar bayangan tingkat di atasnya). Dengan ini jarak vertikal 10 cm antar-dek tetap dipakai, total tinggi alat tetap ±(30 cm + ~28 cm tube + kaki) ≈ **65–75 cm**. Cukup ringkas untuk dibawa.

### 2.3 Jebakan 3: kedalaman air di pipa itu MILIMETER, bukan centimeter

Hasil perhitungan kedalaman normal (Manning, pipa kotak, n=0.010):

| Geometri | h @ 5 L/menit | h @ 10 | h @ 17 |
| :--- | ---: | ---: | ---: |
| Pipa kotak 40×40 mm, S=1% | 7.0 mm | 11.2 mm | 16.4 mm |
| Pipa kotak 40×40 mm, S=0.5% | 8.8 mm | 14.3 mm | 21.2 mm |
| **Pipa CUSTOM 40×20 mm, S=1%** (final) | 7.0 mm | 11.2 mm | 16.4 mm |

> Catatan pipa custom 20 mm: @S=0.5% kapasitasnya cuma 15.8 L/menit → pompa 2000 L/jam membuatnya **penuh/surcharged** dan Manning tidak berlaku. Karena itu kemiringan wajib **1%** (kapasitas 22.3 L/menit). Kedalaman 7–16 mm makin memperkuat keputusan: **level sistem dibaca di tampungan, bukan di pipa**.

Akurasi absolut JSN-SR04T = **±10 mm**. Kalau kedalaman cuma 7–21 mm, errornya 50–140% — level tidak bisa dipakai.

**Solusi dua lapis:**
1. **Gate/ambang kecil di outlet tiap tampungan** (sekat naik-turun ±15–20 mm). Ini menaikkan muka air tampungan jadi **40–120 mm** — deep & stabil untuk sensor, sekaligus jadi kendali skenario (buka = normal, tutup = sumbatan/backwater).
2. **Pemisahan peran pengukuran:**
   - **Level (sistem):** JSN + stilling well di tampungan (air dalam & tenang) → ini yang masuk ke fuzzy.
   - **Manning (validasi):** diukur MANUAL — kedalaman di pipa pakai mistar yang dipasang permanen di dinding pipa + debit pakai metode volumetrik (gelas ukur + stopwatch) di outlet bawah. Keduanya dicek saat setup, bukan real-time. Jadi klaim "Manning tervalidasi" tetap terpenuhi tanpa memaksa JSN bekerja di luar kemampuannya.

> `chan_mm` per node = tinggi air maksimum tampungan (mis. 120 mm), dikalibrasi saat setup — bukan tinggi pipa.

---

## 3. SPESIFIKASI KONSTRUKSI

| Bagian | Spesifikasi |
| :--- | :--- |
| Wadah besar (reservoir) | Panjang ±1 m (sembak/ray planter/sommaier), volume ≥ 25 L |
| Rangka | Besi hollow / kayu 3 tingkat, tinggi dek 30 / 20 / 10 cm, susunan TANGGA (offset horizontal tiap dek maju ke depan) |
| Pipa per tingkat | **Pipa CUSTOM 40 mm × 20 mm** (lebar × kedalaman), panjang 30 cm, shim **kemiringan 1%** — @S=0.5% kapasitas pipa hanya 15.8 L/menit (penuh/surcharged di Q pompa 2000 L/jam, Manning invalid); @S=1% kapasitas 22.3 L/menit, aman |
| Tampungan per tingkat | Kotak ±100×100×150 mm (bisa dari pipa kotak 4" sepanjang 10 cm), outlet diberi **gate naik-turun** |
| Stilling well | PVC 2" Ø63 mm, panjang ±28 cm, tenggelam 20 mm |
| Pompa | Submersible akuarium 1000–2000 L/jam + ball valve pengatur |
| Jalur | Selang ½": pompa → tampungan T1; balik dari T3 → reservoir |
| Posisi sensor | Di atas stilling well, di tepi luar masing-masing tampungan |
| Tambahan ukur | Mistar baja permanen di dinding pipa & tampungan tiap tingkat |

**Jarak vertikal antar dek:** 10 cm (30→20→10) tetap sesuai konsep kamu — asalkan tampungan di tepi luar.

---

## 4. PARAMETER OPERASI (HASIL HITUNG)

| Kondisi | Q (L/menit) | Level tampungan | Keterangan |
| :--- | ---: | :--- | :--- |
| Baseline | 8–10 (valve 50%) | 35–45% kapasitas | Q_ref dihitung di sini |
| Kiriman (buka valve penuh) | 15–17 | 55–70%, muncul berurutan T1→T2→T3 | lag 12–36 detik per tampungan |
| Sumbatan (gate T3 ditutup parsial) | tetap | T3 naik mendadak; debit efektif T3 turun | signature BACKWATER |
| Lokal (guyur T2) | — | hanya T2 naik | signature LOKAL |

**Residence time tampungan** (1–2 L @ 5–17 L/menit) = **12–36 detik** → gelombang kiriman terlihat berpindah T1→T2→T3 dalam hitungan puluhan detik. **Ini durasi demo yang sempurna**: juri benar-benar melihat "hulu naik duluan, hilir menyusul".

---

## 5. KELEBIHAN DESAIN KASKADE vs LINEAR (bukti memilih ini)

1. **Portabel** — satu bingkai ±1 m × 0.4 m × 0.75 m, muat bagasi mobil. Linear 3 m itu siksaan untuk dibawa ke lokasi presentasi offline.
2. **Lag alami** — jatuh 10 cm antar tingkat + tampungan = gelombang terlihat merambat. Di linear lurus, perubahan hulu hilir hampir seketika (demo-nya kurang dramatis).
3. **Titik kontrol per tingkat** — gate di tiap tampungan membuat skenario S3/S4/S5 bisa dipicu secara independen tanpa membongkar apapun.
4. **Visual** — menyerupai tanggul/saluran bertingkat, gampang dijelaskan ke juri non-teknis.

---

## 6. PROSEDUR UJI S1–S6 (tetap, dipetakan ke kaskade)

Persiapan tiap sesi: isi reservoir, nyalakan pompa, tunggu **5 menit** stabil, nol-kan offset sensor (air tenang, semua gate normal), catat suhu air.

| Kode | Skenario | Aksi fisik | Data yang direkam |
| :--- | :--- | :--- | :--- |
| S1 | Baseline | Valve 50%, semua gate normal | Level & debit semua node → `Q_ref` per node |
| S2 | Leading indicator | TANPA mengubah air: injeksi `tp` 0.2→8.0 di gateway | Status sebelum/sesudah injeksi per node + timestamp |
| S3 | Banjir kiriman | Buka valve penuh mendadak | Waktu mulai naik T1/T2/T3 → **lag time terukur** vs kelas KIRIMAN_HULU |
| S4 | Sumbatan hilir | Turunkan gate T3 sampai parsial | T3 naik + debit efektif T3 turun → BACKWATER + catatan debit |
| S5 | Banjir lokal | Guyur air hanya di T2 | Kelas LOKAL |
| S6 | Kritis serentak | Valve penuh + semua gate dinaikkan | SERENTAK/BAHAYA + alarm semua kanal |

Format rekaman per run (CSV di LittleFS gateway + salinan ke server):

```
waktu_iso, run_id, node_id, level_mm, chan_mm, level_persen, v_m_s, q_lpm,
q_ref_lpm, indeks_debit, rain_tp_mm3h, risk, status, level_l1,
kelas_pola, rssi_dbm, snr_db, seq, paket_hilang, catatan_manual
```

**Validasi Manning (manual, sekali per setelan valve):** kedalaman di pipa dibaca dari mistar permanen, debit dari volumetrik 60 detik di outlet T3, 3× pengulangan. Tabel galat Manning-vs-terukur dilaporkan apa adanya.

---

## 7. KETERBATASAN YANG WAJIB DILAPORKAN (juri akan bertanya)

1. Kaskade bukan saluran seragam — Manning hanya divalidasi **per run pipa 30 cm**, dan itu pun mendekati uniform flow, bukan persis.
2. Skala tidak mereproduksi Reynolds/Froude sungai asli. Testbed memvalidasi **logika sistem**, bukan hidrolika lapangan.
3. Akurasi absolut JSN (±10 mm datasheet) dilaporkan; yang diandalkan adalah kestabilan pembacaan di stilling well (repeatability diukur: 60 sampel air tenang → simpangan baku dilaporkan).
4. Hysteresis ambang status (mis. naik SIAGA di 60%, turun di 55%) dipakai agar status tidak berkedip.
