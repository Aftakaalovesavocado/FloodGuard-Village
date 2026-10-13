	# LISTING MEDIAN PROTOTIPE FGV — PER 1 UNIT (tanpa elektronik)

> Media uji fisik 1 paket node. Direplikasi ×3 (Hulu/Tengah/Hilir).
> **Tidak termasuk:** JSN-SR04T, ESP32, LoRa, resistor — lihat `BOM_PENGADAAN.md` bagian A.
> Geometri final: bak 60 cm → pompa+selang → saluran **100 mm lebar × 30 mm dinding, panjang 40 cm, kemiringan 1° (drop 0.7 cm)** → tampungan **15×10×15 cm TRANSPARAN** → stilling well → luapan jatuh bebas balik ke bak.
> Harga = estimasi pasar 2026 — **verifikasi saat checkout.**

---

## A. KOMPONEN WAJIB PER UNIT

| # | Item | Spek / Detail | Qty | Est. Rp | Catatan |
| --: | :--- | :--- | --: | --: | :--- |
| 1 | Bak reservoir | Plastik/serbaguna ±60×40×25 cm, kuat menahan ±25 L | 1 | 55.000 | Pompa + air balik di sini |
| 2 | Pompa celup | **2500–3000 L/jam** (mis. HJ-3000/HJ-3700), kepala angkat ≥1 m | 1 | 75.000 | Jangan 1000–2000 L/jam — kedalaman aliran jadi <6 mm (lihat diskusi 16/9) |
| 3 | Ball valve ½" | Pengatur debit manual (S1 baseline vs S3 kiriman) | 1 | 15.000 | Wajib — ini "tangan" simulasi debit naik/turun |
| 4 | Selang ½" bening | Panjang 2 m + 2 klem selang + nipal | 1 set | 30.000 | Jalur pompa → ujung hulu saluran |
| 5 | **Saluran 100×30×400 mm** | Opsi A: **akrilik bening 3 mm** (potong: dasar 40×10, dinding kiri-kanan 40×3, 2 penutup ujung) + lem akrilik | 1 set | 125.000 | Opsi B hemat: pipa kotak uPVC 100×50 dipotong tinggi 3 cm (~Rp 35k, opaque) |
| 6 | **Tampungan 15×10×15 cm TRANSPARAN** | Akrilik bening 3–5 mm, jasa potong+lem (atau kotak akrilik jadi) | 1 | 75.000 | **Wajib transparan** — cross-check visual level vs pembacaan JSN + tampil di video. Sensor tidak terpengaruh (yang dibaca JSN = air di dalam tube) |
| 7 | Stillling well | **Pipa akrilik bening Ø63 mm (2"), panjang 50 cm** | 1 | 50.000 | Ujung bawah terbuka, tenggelam 3–5 cm. Alt hemat: PVC 2" AW putih (Rp 20k, opaque) |
| 8 | Tutup tube + bracket | Akrilik/pelat kecil: lubang seukuran transducer JSN + **lubang ventilasi 3–5 mm** | 1 set | 20.000 | Sensor duduk di tutup; tanpa ventilasi level di tube lag |
| 9 | **Sekat geser** (simulasi sumbatan) | Pelat akrilik 3 mm ±6×16 cm, meluncur di rel/slot di bibir luapan tampungan | 1 set | 20.000 | Dinaikkan = sumbatan → level naik, debit efektif turun |
| 10 | Rangka | Kayu 3×4 atau besi hollow 20×20: 4 kaki + 2 palang (dek saluran + dek tampungan) | 1 set | 85.000 | Tinggi dek saluran ±30 cm; pastikan kaku (goyang = bacaan JSR bergoyang) |
| 11 | Sekrup, siku L, baut | Perakitan rangka + bracket tube & tampungan | 1 lot | 20.000 | |
| 12 | Mistar baja 30 cm | **Ditempel permanen vertikal** di dinding saluran & tampungan | 2 | 25.000 | Kalibrasi JSN + validasi Manning manual |
| 13 | Lem PVC / silikon / sealant | Anti-bocor sambungan selang, tampungan, tube | 1 | 25.000 | Uji bocor WAJIB sebelum demo |
| 14 | Meteran + spidol permanen | Penanda elevasi, label HULU/TENGAH/HILIR | 1 | 15.000 | |
| | **TOTAL PER UNIT** | | | **±610.000** | |

## B. PERKAKAS / KONSUMABEL DIPAKAI BERSAMA (beli 1× untuk 3 unit)

| # | Item | Qty | Est. Rp |
| --: | :--- | --: | --: |
| 1 | Gelas ukur 1 L (debit volumetrik — ground truth Manning) | 1 | 20.000 |
| 2 | Stopwatch (bisa pakai HP — catat saja) | – | 0 |
| 3 | Bor + mata bor 3–8 mm (lubang ventilasi, slot sekat, jalur selang) | 1 | (pinjam) |
| 4 | Gergaji potong akrilik/PVC + penggaris siku | 1 | (pinjam) |
| 5 | Ember cadangan 20–30 L (penggantian air / flush) | 1 | 25.000 |
| 6 | Air bersih isi ulang (isi awal ±25 L/unit + cadangan) | ±100 L | 20.000 |

## C. REKAP

```
Per unit (opsi akrilik, direkomendasikan untuk demo) : ± Rp 635.000
Per unit (opsi hemat: saluran & tube PVC, tampungan plastik) : ± Rp 480.000
3 UNIT opsi rekomendasi                              : ± Rp 1.905.000
Perkakas bersama                                     : ± Rp   65.000
─────────────────────────────────────────────────────────────────
TOTAL MEDIAN 3 UNIT (tanpa elektronik)               : ± Rp 1.970.000
```

Elektronik (BOM A): ± Rp 393.000 → **grand total prototipe ± Rp 2.363.000.**
RAB resmi (5 sheet) tidak disertakan di repo publik — data internal.

---

## D. CATATAN FABRIKASI (untuk tukang/pembuat)

1. **Saluran miring 1°** = drop 0.7 cm sepanjang 40 cm → dek hulu sedikit lebih tinggi dari dek tampungan. Jangan lebih curam dari 2°.
2. **Bibir luapan tampungan** diberi slot/sayap untuk sekat geser; posisi normal sekat = rendah (level ±20%); dinaikkan saat simulasi sumbatan.
3. **Tube duduk sampai dasar tampungan**, tegak lurus, dipatok bracket ke rangka (jangan ke dinding tampungan — dinding akrilik tidak boleh diberi beban).
4. **Lubang yang perlu dibor sebelum lem:** jalur keluar selang di bak, slot sekat di bibir tampungan. Tutup tube: lubang transducer + lubang ventilasi 3–5 mm.
5. **Tes bocor 10 menit** dengan air penuh SEBELUM dipasang sensor — semua sambungan akrilik/lem.
6. **Urutan perakitan:** rangka → bak → pompa+selang → saluran (cek kemiringan waterpass) → tampungan → tube → mistar tempel (nol-kan terhadap dasar) → tes aliran 5 menit → baru elektronik.
7. Semua ukuran saluran & tampungan **diukur ulang setelah jadi** (toleransi potong ±2 mm) — angka ini yang masuk firmware (`b`, `chan_mm`), bukan angka di kertas.

## E. CROSS-CHECK SAAT UNIT 1 SELESAI (sebelum replikasi ×2)

- [ ] Air mengalir antar sepanjang saluran tanpa genang/dangkal ekstrem
- [ ] Level tampungan stabil di sekitar 20–30% dengan sekat normal
- [ ] Sekat dinaikkan → level naik halus sampai >80%, luapan mengecil
- [ ] Tube: level air di dalam tube tampak sama dengan tampungan (transparan = langsung terlihat)
- [ ] Tidak bocor di semua sambungan setelah 10 menit
- [ ] Baru duplikat ke unit 2 & 3
