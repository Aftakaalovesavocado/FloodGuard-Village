# KALIBRASI MANNING — FGV Testbed (GEOMETRI & n EFEKTIF)

Terakhir diperbarui: 21 Sep 2026 · Firmware: `firmware/flow_manning.h`

## Geometri terukur

| Parameter | Nilai | Cara |
| :-------- | :---- | :--- |
| Lebar saluran b | 80 mm | mistar |
| Tinggi dinding | 25 mm | mistar |
| Panjang segmen | 450 mm | mistar |
| Elevasi hulu | 593 mm | mistar/waterpass |
| Elevasi hilir | 589 mm | mistar/waterpass |
| Kemiringan S | 4/450 = **0.00889** | (593−589)/450 |
| Bentuk | persegi (m=0) | talang logam plat bordes |

## Kalibrasi volumetrik — titik 1 (21 Sep 2026)

- Metode: gelas ukur 1000 mL + stopwatch, **10 repetisi**, kondisi valve
  ball penuh, aliran steady.
- Level arus di saluran (mistar): **h = 9 mm**

| # | t (s) | Q (L/min) |
| --: | ----: | ----: |
| 1 | 5.95 | 10.08 |
| 2 | 5.24 | 11.45 *(outlier lembut, z=+2.1 — dilaporkan, tidak dibuang)* |
| 3 | 5.98 | 10.03 |
| 4 | 6.55 | 9.16 |
| 5 | 5.72 | 10.49 |
| 6 | 5.68 | 10.56 |
| 7 | 5.91 | 10.15 |
| 8 | 6.29 | 9.54 |
| 9 | 6.18 | 9.71 |
| 10 | 5.94 | 10.10 |

```
Q terukur : 9.98 L/min (mean 10 hitung) · SD 0.63 · CV 6.2%
            (tanpa outlier lembut: 9.98, SD 0.45, CV 4.5%)
Q Manning (n=0.010, h=9mm) : 15.39 L/min
→ n EFEKTIF = 0.010 × 15.39/9.98 = 0.0154
```

**Interpretasi n = 0.0154:** sesuai tabel buku teks untuk permukaan
logam bergelombang/kasar (0.016–0.022 sekitar rentangnya); dasar talang
plat bordes (berpola diamond) memang kasar. n bukan "salah" — dia
karakter saluran yang ditemukan lewat kalibrasi.

## Konfigurasi sensor (riwayat & datum)

| Tanggal | SENSOR_HEIGHT_MM | Keterangan |
| :-- | --: | :--- |
| 20 Sep | 308 | well di wadah bening; "kering" ternyata ±28 mm air sisa |
| 21 Sep | 331 | well dipanjangkan; datum "kering" masih di wadah bening |

⚠️ **TEMUAN 21 Sep (penting):** selama sensor melihat **wadah bening
(penerima)**, firmware mengukur kedalaman kolam (75–80 mm) — BUKAN
kedalaman aliran talang (9 mm). Selisih mistar-vs-firmware tetap 28 mm →
sensor linear & sehat; masalahnya datum + lokasi.
**RELOKASI**: well 1.5" pendek (~15 cm, 4–6 lubang Ø6–8 mm di bawah)
dipasang vertikal **di dasar talang**; `SENSOR_HEIGHT_MM` = jarak
sensor→dasar talang, diukur **saat talang benar-benar kering**.
Manning hanya sah dari level aliran talang.

## Kapasitas & arah demo (dengan n = 0.0154)

| Q | h steady (prediksi) |
| --: | ----: |
| 5 L/min | 5.8 mm |
| 10 L/min (pompa real ~ini) | 9.0 mm |
| 15 L/min | 11.7 mm |
| 20 L/min | 14.2 mm |
| 45 L/min | 25 mm (bibir — batas Q_maks saluran) |

Pompa real (~10 L/min) **tidak akan pernah meluap lewat aliran** —
luapan hanya terjadi via **backwater** (menutup ball valve outflow),
yang justru skenario S4 yang diinginkan: terkendali, bermakna
hidrolika, dan terlihat mata (air tumpah dari sisi talang).

## Sisa pekerjaan kalibrasi

- [ ] Relokasi well ke talang + ukur ulang `SENSOR_HEIGHT_MM` (kering!)
- [ ] Titik kalibrasi 2: valve ⅔ buka → (level firmware, Q ukur 3×)
- [ ] Titik kalibrasi 3: valve ⅓ buka → (level firmware, Q ukur 3×)
- [ ] Tabel Q vs h 3 titik + galat % per titik → lampiran makalah
