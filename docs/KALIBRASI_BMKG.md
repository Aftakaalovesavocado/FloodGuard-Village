# FGV — TEMUAN KALIBRASI DATA BMKG (SUMBER KEBENARAN)

**Status:** DIVERIFIKASI EMPIRIS — 16 Sep 2026
**Metode:** Query langsung `GET https://api.bmkg.go.id/publik/prakiraan-cuaca?adm4=<kode>` (butuh header User-Agent, kalau tidak → HTTP 403)
**Sampel:** 7 wilayah × 20 entri prakiraan = **140 entri**

| Kode adm4 | Desa | Kecamatan |
| :--- | :--- | :--- |
| 00.00.00.0000 | Wilayah_Target | Kecamatan_Target *(lokasi target)* |
| 00.00.00.0000 | Gambir | Gambir |
| 00.00.00.0000 | Sukarasa | Sukasari |
| 00.00.00.0000 | Pasar Baru | Medan Kota |
| 00.00.00.0000 | Temas | Batu |
| 00.00.00.0000 | Serangan | Denpasar Selatan |
| 00.00.00.0000 | Manggar | Balikpapan Timur |

---

## 1. TEMUAN

### 1.1 Field `tp` = curah hujan per 3 JAM (mm), bukan mm/hari

Distribusi terukur:

| `weather_desc` | n | min `tp` | max `tp` | rata-rata |
| :--- | ---: | ---: | ---: | ---: |
| Hujan Ringan | 9 | 1.2 | 4.0 | 1.91 |
| Berawan | 33 | 0.0 | 1.0 | 0.22 |
| Cerah Berawan | 24 | 0.0 | 0.7 | 0.13 |
| Udara Kabur | 9 | 0.0 | 0.7 | 0.28 |
| Kabut/Asap | 2 | 0.0 | 0.7 | 0.35 |
| Cerah | 63 | 0.0 | 0.5 | 0.06 |

**Kesimpulan 1:** `tp` adalah akumulasi curah hujan untuk satu periode prakiraan 3 jam. Nilai maksimum yang teramati hanya **4.0 mm**. Noise floor: kondisi tanpa hujan tetap bisa `tp` sampai **1.0 mm** (label "Berawan").

**Kesimpulan 2 (BUG):** Membership function hujan pada firmware MVP berkode `trapLeftMF(x,10,25) / triangleMF(x,15,35,55) / trapRightMF(x,40,60)`. Domainnya 10–60 mm. Karena input nyata hanya 0–4 mm, maka **derajat keanggotaan "Ringan" selalu 1.0 dan "Sedang"/"Lebat" selalu 0** untuk setiap kondisi cuaca — termasuk saat BMKG memprakirakan hujan. Variabel hujan bersifat **konstan/tak berfungsi (dead input)**.

**Dampak:** klaim "curah hujan BMKG sebagai *leading indicator* dengan bobot lebih besar dari tinggi air" **tidak tereksekusi** di sistem. FGV versi MVP berjalan murni reaktif terhadap ketinggian air & debit.

### 1.2 Inkonsistensi label vs nilai pada produk BMKG sendiri

BMKG memberi label "Hujan Ringan" untuk `tp` sampai 4.0 mm/3jam. Bila dikonversi (×8) itu setara **32 mm/hari**, yang masuk kategori *hujan sedang* menurut klasifikasi harian BMKG. Jadi label `weather_desc` **tidak boleh** dipakai sebagai basis fuzzifikasi.

**Keputusan desain:** fuzzifikasi berbasis **nilai numerik `tp`**, bukan label `weather_desc`.

### 1.3 Sumber alternatif yang TIDAK tersedia

`https://data.bmkg.go.id/DataMKG/MEWS/DigitalForecast/DigitalForecast-JawaBarat.xml` → sudah tidak melayani file XML (mengembalikan halaman landing HTML 25.8 KB, bukan XML; elemen `<name>rain</name>` tidak ditemukan). **Jangan pakai path ini di dokumen/kode.** Satu-satunya jalur `tp` numerik yang terverifikasi adalah endpoint `publik/prakiraan-cuaca`.

---

## 2. MF CURAH HUJAN BARU (v2 — TERKALIBRASI)

Basis konversi: klasifikasi curah hujan harian BMKG ÷ 8 → periode 3 jam.

| Kategori harian | mm/hari | ekuivalen mm/3jam |
| :--- | :--- | :--- |
| Hujan ringan | 5–20 | 0.6–2.5 |
| Hujan sedang | 20–50 | 2.5–6.25 |
| Hujan lebat | 50–100 | 6.25–12.5 |
| Hujan sangat lebat | >100 | >12.5 |

```
Ringan : trapLeftMF (x, 1.0, 2.5)     -> 1.0 penuh pada x <= 1.0, turun ke 0 di x = 2.5
Sedang : triangleMF (x, 2.0, 4.0, 7.0)
Lebat  : trapRightMF(x, 5.5, 10.0)
```

**Uji balik terhadap data terukur:**

| `tp` (mm/3jam) | Ringan | Sedang | Lebat | Hasil | Konteks |
| ---: | ---: | ---: | ---: | :--- | :--- |
| 0.2 | 1.000 | 0.000 | 0.000 | Ringan | Prakiraan "Cerah" (kondisi hari ini di Wilayah_Target) |
| 1.0 | 1.000 | 0.000 | 0.000 | Ringan | Noise floor "Berawan" → tidak memicu alarm ✓ |
| 4.0 | 0.000 | 1.000 | 0.000 | Sedang | Nilai maksimum teramati ("Hujan Ringan" label BMKG) |
| 6.5 | 0.000 | 0.714 | 0.400 | Sedang–Lebat | Ambang hujan lebat harian |
| 10.0 | 0.000 | 0.000 | 1.000 | Lebat | Hujan lebat berkelanjutan |
| 25.0 | 0.000 | 0.000 | 1.000 | Lebat (jenuh) | Sangat lebat → tetap 1.0, aman dari overflow |

Desain ini memenuhi sifat penting: **noise floor BMKG (≤1.0 mm tanpa hujan) tidak memicu alarm**, sementara hujan lebat (≥5.5 mm/3jam) mendorong risiko ke level SIAGA/BAHAYA meskipun air belum naik — inilah *lead time* yang dijanjikan.

---

## 3. IMPLIKASI TERHADAP DEBIT (MASALAH SEJENIS)

Firmware MVP memfuzzifikasi **debit dalam L/menit** dengan MF `trapLeftMF(lpm,15,40) / triangleMF(lpm,20,45,80) / trapRightMF(lpm,60,90)` — dirancang untuk pipa 1" dengan laju 0–60 L/menit.

Pada **testbed skala lab** (pompa akuarium, saluran uji), debit operasional hanya **1–15 L/menit** → variabel debit juga akan selalu bernilai "Lambat". Skala laboratorium dan skala lapangan berbeda ~10×.

**Solusi (dipakai di FGV v2):** variabel kedua tidak lagi dinyatakan dalam satuan absolut, melainkan **Indeks Debit** `I_Q = Q / Q_ref`, dengan `Q_ref` = debit baseline terukur di lokasi/node tersebut saat kondisi normal.

```
Lambat : trapLeftMF (x, 0.8, 1.3)
Sedang : triangleMF (x, 1.0, 1.6, 2.6)
Deras  : trapRightMF(x, 2.0, 3.0)
```

Alasan (harus disebut saat presentasi):
1. **Portabel antar skala** — rule base yang sama berlaku di ember lab (Q_ref = 2 L/menit) maupun di saluran asli (Q_ref = 30 L/menit), tanpa penulisan ulang threshold.
2. **Sensor-agnostik** — nilai `Q` boleh berasal dari Persamaan Manning, radar Doppler, atau flowmeter; rule base tidak berubah.
3. **Konsisten dengan dokumen kita sendiri** yang menyatakan bahwa baseline tiap sungai berbeda dan threshold wajib dikalibrasi lapangan. Indeks rasio membuat pernyataan itu secara arsitektural benar, bukan sekadar catatan kaki.

---

## 4. CATATAN UNTUK REVISI DOKUMEN TAHAP 3

Revisi wajib pada makalah & rancang bangun (klaim lama → klaim terkoreksi):

| Lokasi | Klaim lama | Revisi |
| :--- | :--- | :--- |
| Spesifikasi teknis | "Curah Hujan BMKG (R): Ringan 0–20 mm/hari, Sedang 15–50, Lebat >40" | "Curah hujan (mm/3jam periode prakiraan): Ringan ≤1.0–2.5, Sedang 2.5–6.25, Lebat ≥5.5. Dikalibrasi terhadap 140 sampel empiris dari 7 wilayah." |
| Spesifikasi teknis | "Debit/Kecepatan (Q): Lambat 0–1.0 m/s, Sedang 0.8–2.5, Deras >2.0" | "Indeks Debit I_Q = Q/Q_ref: Lambat ≤0.8, Sedang ~1.6, Deras ≥2.0 (rasio terhadap baseline terukur)" |
| Hasil uji | "presipitasi BMKG ... menjadikan kontribusi pemicu early alert hingga 40% lebih awal" | Diganti dengan angka hasil simulasi engine v2 (lihat `sim/HASIL_VALIDASI.md`) |

**Prinsip:** setiap angka di dokumen final harus punya jejak ke pengukuran atau simulasi yang bisa diperiksa. Jangan ada angka yang tidak bisa kita pertanggungjawabkan saat ditanya "dari mana ini?"
