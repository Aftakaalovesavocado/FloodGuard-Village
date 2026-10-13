# Hasil Validasi FGV Engine v2

```

==============================================================================
VALIDASI ENGINE FLOODGUARD VILLAGE v2
==============================================================================
Dijalankan: 16 Sep 2026 13:18
Python     : 3.11.16

------------------------------------------------------------------------------
BAGIAN 1 — UJI MF BARU TERHADAP DATA BMKG NYATA
------------------------------------------------------------------------------

Nilai acuan (hasil query API BMKG, 140 sampel / 7 wilayah):
  - kondisi tanpa hujan (label Cerah/Berawan) : tp = 0.0 - 1.0 mm/3jam
  - label 'Hujan Ringan' BMKG                 : tp = 1.2 - 4.0 mm/3jam
  - ambang hujan lebat (50 mm/hari / 8)       : tp = 6.25 mm/3jam

Uji balik MF hujan v2:

  tp(mm/3j)  Ringan  Sedang   Lebat   Hasil     Keterangan
  ------------------------------------------------------------------
      0.2    1.000   0.000   0.000   RINGAN    prakiraan Cerah (kondisi hari ini)
      1.0    1.000   0.000   0.000   RINGAN    noise floor 'Berawan' - harus TIDAK memicu
      2.5    0.000   0.250   0.000   SEDANG    batas hujan ringan/sedang
      4.0    0.000   1.000   0.000   SEDANG    nilai maksimum teramati di 140 sampel
      6.5    0.000   0.167   0.222   LEBAT     ambang hujan lebat
     10.0    0.000   0.000   1.000   LEBAT     hujan lebat
     25.0    0.000   0.000   1.000   LEBAT     sangat lebat - harus jenuh, tidak overflow

------------------------------------------------------------------------------
BAGIAN 2 — BUKTI BUG MF LAMA (hujan sebagai input mati)
------------------------------------------------------------------------------

Kondisi sama (level 20%, debit 30 L/menit), hanya curah hujan yang diubah:

  MF LAMA (domain 10-60 mm, mm/hari diasumsikan):
    tp = 0.2 mm  -> risk =   7.50  (AMAN)
    tp = 8.0 mm  -> risk =   7.50  (AMAN)
    SELISIH      =   0.00  <-- NOL = input hujan tidak berpengaruh sama sekali

  MF BARU v2 (domain terkalibrasi 0-10 mm/3jam):
    tp = 0.2 mm  -> risk =   5.00  (AMAN)
    tp = 8.0 mm  -> risk =  50.00  (SIAGA)
    SELISIH      =  45.00  dengan muka air yang SAMA

  -> KESIMPULAN: pada versi lama, sistem hanya reaktif (butuh air naik).
     Pada v2, prakiraan hujan lebat dapat menaikkan status sebelum air naik.

------------------------------------------------------------------------------
BAGIAN 2B — RISIKO JENUH (SATURASI) & LAPISAN KESELAMATAN
------------------------------------------------------------------------------

Masalah: defuzzifikasi weighted-average hanya merata-ratakan rule yang menyala.
Rule berkonsekuen tinggi yang TIDAK menyala tidak dapat menarik risiko ke atas.
Akibatnya, pada kasus ekstrem satu variabel, risiko jenuh di nilai rendah.

  level(%)  I_Q   hujan   risk TANPA        risk DENGAN      status akhir
                          lapisan aman     lapisan aman
  ------------------------------------------------------------------------
     100   0.5   ringan    40.0 (WASPADA)       90.0 (BAHAYA)    saluran PENUH, debit rendah  <-- diperbaiki
      95   0.6   ringan    40.0 (WASPADA)       90.0 (BAHAYA)    saluran hampir penuh  <-- diperbaiki
      80   0.5   ringan    40.0 (WASPADA)       75.0 (BAHAYA)    muka air tinggi, aliran pelan  <-- diperbaiki
      78   2.6   ringan    80.0 (BAHAYA)        80.0 (BAHAYA)    muka air tinggi + debit puncak
      60   0.5   ringan    27.5 (WASPADA)       27.5 (WASPADA)   muka air sedang, aliran pelan
      40   1.0   ringan    15.0 (AMAN)          15.0 (AMAN)      kondisi normal

  ATURAN LAPISAN KESELAMATAN (eksplisit, bisa dipertanggungjawabkan):
    - muka air >= 75% kapasitas  -> risk minimal 75 (BAHAYA)
    - muka air >= 90% kapasitas  -> risk minimal 90
    - I_Q >= 2.5 DAN level >= 60% -> risk minimal 70

  Argumennya: ambang 75% kapasitas adalah titik di mana kenaikan air sudah
  mengancam luapan secara langsung - kondisi fisik ini tidak boleh bisa
  'dinegosiasikan' oleh bobot fuzzy. Logika fuzzy tetap menangani bagian
  ANTISIPATIF (prakiraan hujan & tren) yang justru berada di bawah 75%.

------------------------------------------------------------------------------
BAGIAN 3 — TABEL LENGKAP 27 KOMBINASI LAYER 2
------------------------------------------------------------------------------

  Hulu  Tengah  Hilir   Kelas Pola        Contoh situasi
  ------------------------------------------------------------------------
  Tinggi Tinggi  Tinggi   SERENTAK          seluruh saluran naik bersamaan
  Tinggi Tinggi  Sedang   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Tinggi Tinggi  Rendah   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Tinggi Sedang  Tinggi   CAMPURAN          pola tak dikenal - perlu verifikasi lapangan
  Tinggi Sedang  Sedang   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Tinggi Sedang  Rendah   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Tinggi Rendah  Tinggi   CAMPURAN          pola tak dikenal - perlu verifikasi lapangan
  Tinggi Rendah  Sedang   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Tinggi Rendah  Rendah   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Sedang Tinggi  Tinggi   BACKWATER         sumbatan hilir mendorong air balik
  Sedang Tinggi  Sedang   LOKAL             hujan setempat pada satu segmen
  Sedang Tinggi  Rendah   LOKAL             hujan setempat pada satu segmen
  Sedang Sedang  Tinggi   BACKWATER         sumbatan hilir mendorong air balik
  Sedang Sedang  Sedang   SERENTAK          seluruh saluran naik bersamaan
  Sedang Sedang  Rendah   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Sedang Rendah  Tinggi   BACKWATER         sumbatan hilir mendorong air balik
  Sedang Rendah  Sedang   CAMPURAN          pola tak dikenal - perlu verifikasi lapangan
  Sedang Rendah  Rendah   KIRIMAN_HULU      gelombang dari hulu, hilir belum terdampak
  Rendah Tinggi  Tinggi   BACKWATER         sumbatan hilir mendorong air balik
  Rendah Tinggi  Sedang   LOKAL             hujan setempat pada satu segmen
  Rendah Tinggi  Rendah   LOKAL             hujan setempat pada satu segmen
  Rendah Sedang  Tinggi   BACKWATER         sumbatan hilir mendorong air balik
  Rendah Sedang  Sedang   BACKWATER         sumbatan hilir mendorong air balik
  Rendah Sedang  Rendah   LOKAL             hujan setempat pada satu segmen
  Rendah Rendah  Tinggi   BACKWATER         sumbatan hilir mendorong air balik
  Rendah Rendah  Sedang   BACKWATER         sumbatan hilir mendorong air balik
  Rendah Rendah  Rendah   AMAN              kondisi normal

  REKAP: AMAN=1 | BACKWATER=8 | CAMPURAN=3 | KIRIMAN_HULU=8 | LOKAL=5 | SERENTAK=2
  Total kombinasi: 27 (harus 27)

------------------------------------------------------------------------------
BAGIAN 4 — SKENARIO END-TO-END (Layer 1 + Layer 2)
------------------------------------------------------------------------------

  S1 — Normal / baseline (cuaca cerah)
      HULU   level= 12.0%  I_Q=1.00  hujan=0.2 mm/3j  ->  risk=  5.0  (AMAN, level L1=0)
      TENGAH level= 10.0%  I_Q=1.00  hujan=0.2 mm/3j  ->  risk=  5.0  (AMAN, level L1=0)
      HILIR  level= 11.0%  I_Q=1.00  hujan=0.2 mm/3j  ->  risk=  5.0  (AMAN, level L1=0)
      >> KELAS POLA : AMAN
         Semua node pada level Rendah
         Tindakan: Monitoring rutin

  S2 — Prakiraan hujan lebat, air MASIH rendah (uji leading indicator)
      HULU   level= 18.0%  I_Q=1.05  hujan=8.0 mm/3j  ->  risk= 51.4  (SIAGA, level L1=1)
      TENGAH level= 16.0%  I_Q=1.05  hujan=8.0 mm/3j  ->  risk= 51.4  (SIAGA, level L1=1)
      HILIR  level= 15.0%  I_Q=1.05  hujan=8.0 mm/3j  ->  risk= 51.4  (SIAGA, level L1=1)
      >> KELAS POLA : SERENTAK
         Kenaikan merata sepanjang saluran
         Tindakan: Aktivasi posko siaga desa

  S3 — Banjir kiriman dari hulu (gelombang dalam perjalanan)
      HULU   level= 78.0%  I_Q=2.60  hujan=6.0 mm/3j  ->  risk= 96.2  (BAHAYA, level L1=2)
      TENGAH level= 45.0%  I_Q=1.70  hujan=6.0 mm/3j  ->  risk= 60.0  (SIAGA, level L1=2)
      HILIR  level= 14.0%  I_Q=1.00  hujan=6.0 mm/3j  ->  risk= 27.5  (WASPADA, level L1=1)
      >> KELAS POLA : KIRIMAN_HULU
         Banjir kiriman dari hulu (gelombang bergerak ke hilir)
         Tindakan: Peringatkan warga hilir, amankan barang, siapkan jalur evakuasi

  S4 — Sumbatan di hilir (backwater, debit hilir turun)
      HULU   level= 14.0%  I_Q=1.00  hujan=3.0 mm/3j  ->  risk= 20.0  (AMAN, level L1=0)
      TENGAH level= 18.0%  I_Q=1.05  hujan=3.0 mm/3j  ->  risk= 21.4  (AMAN, level L1=0)
      HILIR  level= 72.0%  I_Q=0.55  hujan=3.0 mm/3j  ->  risk= 65.0  (SIAGA, level L1=2)
      >> KELAS POLA : BACKWATER
         Pembendungan / sumbatan di hilir (efek backwater)
         Konfirmasi debit: indikasi sumbatan KUAT: muka air hilir 72% sementara debit turun ke 0.55x baseline
         Tindakan: Kirim tim cek fisik sumbatan/bendung di hilir

  S5 — Hujan lokal hanya di segmen tengah
      HULU   level= 11.0%  I_Q=1.00  hujan=2.0 mm/3j  ->  risk=  5.0  (AMAN, level L1=0)
      TENGAH level= 68.0%  I_Q=2.20  hujan=9.0 mm/3j  ->  risk= 93.2  (BAHAYA, level L1=2)
      HILIR  level= 12.0%  I_Q=1.00  hujan=2.0 mm/3j  ->  risk=  5.0  (AMAN, level L1=0)
      >> KELAS POLA : LOKAL
         Banjir lokal pada satu segmen
         Tindakan: Cek segmen terkait, siapkan pompa lokal

  S6 — Kritis serentak sepanjang saluran
      HULU   level= 88.0%  I_Q=2.80  hujan=10.0 mm/3j  ->  risk=100.0  (BAHAYA, level L1=2)
      TENGAH level= 84.0%  I_Q=2.60  hujan=10.0 mm/3j  ->  risk=100.0  (BAHAYA, level L1=2)
      HILIR  level= 80.0%  I_Q=2.50  hujan=10.0 mm/3j  ->  risk= 99.2  (BAHAYA, level L1=2)
      >> KELAS POLA : SERENTAK
         Kenaikan merata sepanjang saluran
         Tindakan: Aktivasi posko siaga desa


------------------------------------------------------------------------------
BAGIAN 5 — KUANTIFIKASI LEAD TIME (pengganti klaim '40% lebih awal')
------------------------------------------------------------------------------

Simulasi hidrograf: muka air naik linear 20% -> 100% dengan laju 0.4 %/menit,
indeks debit I_Q konstan 1.2, ambang peringatan = risk 50 (SIAGA).

  t(menit)  level(%)   risk TANPA hujan   risk hujan sedang   risk hujan lebat
  ----------------------------------------------------------------------------
        0     20.0              8.1                26.2                56.2   <== SIAGA hanya karena prakiraan hujan
       10     24.0             14.5                34.2                61.4   <== SIAGA hanya karena prakiraan hujan
       20     28.0             17.6                37.6                63.6   <== SIAGA hanya karena prakiraan hujan
       30     32.0             20.3                40.6                65.4   <== SIAGA hanya karena prakiraan hujan
       40     36.0             27.5                49.4                71.2   <== SIAGA hanya karena prakiraan hujan
       50     40.0             27.5                49.4                71.2   <== SIAGA hanya karena prakiraan hujan
       60     44.0             27.5                49.4                71.2   <== SIAGA hanya karena prakiraan hujan
       70     48.0             27.5                49.4                71.2   <== SIAGA hanya karena prakiraan hujan
       80     52.0             32.7                54.7                75.6   <== SIAGA hanya karena prakiraan hujan
       90     56.0             38.1                60.1                79.9   <== SIAGA hanya karena prakiraan hujan
      100     60.0             40.0                61.9                81.2   <== SIAGA hanya karena prakiraan hujan
      110     64.0             40.5                62.5                81.8   <== SIAGA hanya karena prakiraan hujan
      120     68.0             46.2                68.2                86.3   <== SIAGA hanya karena prakiraan hujan
      130     72.0             52.5                74.4                91.2
      140     76.0             75.0                75.0                91.2
      150     80.0             75.0                75.0                91.2
      160     84.0             75.0                75.0                91.2
      170     88.0             75.0                75.0                91.2
      180     92.0             90.0                90.0                91.2
      190     96.0             90.0                90.0                91.2
      200    100.0             90.0                90.0                91.2

  Waktu ambang SIAGA (risk>=50) pertama tercapai:
    - sistem hanya level+debit (hujan diabaikan) : menit 130
    - v2 dengan prakiraan HUJAN SEDANG (3 mm/3j) : menit 80
    - v2 dengan prakiraan HUJAN LEBAT  (8 mm/3j) : menit 0

    LEAD TIME diperoleh, hujan sedang : 50 menit
    LEAD TIME diperoleh, hujan lebat  : 130 menit

  CATATAN KEJUJURAN (wajib disebut saat presentasi):
    a. Ini hasil SIMULASI MODEL, bukan pengukuran lapangan.
    b. Angka lead time bergantung pada laju kenaikan air yang diasumsikan
       (0.4 %/menit di sini). Laju lebih lambat -> lead time lebih besar.
    c. Yang harus dibuktikan di testbed adalah MEKANISMENYA (prakiraan hujan
       saja dapat mengangkat status tanpa air naik), bukan angka menitnya.
    d. Nilai prakiraan hujan diuji lewat injeksi nilai pada gateway (skenario S2).

------------------------------------------------------------------------------
BAGIAN 6 — VERIFIKASI PERHITUNGAN WAKTU MELUAP
------------------------------------------------------------------------------

Kasus uji: level naik linear 1.0 %/menit. Sampel terakhir = 46%.
Acuan yang benar = (100 - 46) / 1.0 = 54.00 menit (bukan 60).
Catatan: estimator memakai level TERAKHIR sebagai titik awal hitung sisa
kapasitas - jadi acuan harus dihitung dari sampel terakhir, bukan dari awal window.

  jumlah sampel        : 13 (window 6.0 menit)
  level awal -> akhir  : 40.0% -> 46.0%
  kemiringan least-sq  : 0.016667 %/detik  = 1.0000 %/menit
  estimasi meluap      : 54.00 menit  (ok)
  acuan benar          : 54.00 menit
  galat                : 0.000 menit

  Uji ketahanan terhadap noise (5 sampel terakhir dinaikkan acak +/-2%):
    trial 1: least-squares =  58.20 menit | metode titik-ujung (MVP) =  45.83 menit
    trial 2: least-squares =  65.48 menit | metode titik-ujung (MVP) =  81.40 menit
    trial 3: least-squares =  61.66 menit | metode titik-ujung (MVP) =  48.55 menit

  -> Metode titik-ujung mudah terlempar oleh satu sampel berisik;
     least-squares jauh lebih stabil. Ini alasan penggantian di v2.

------------------------------------------------------------------------------
BAGIAN 7 — PERSAMAAN MANNING: TABEL SENSITIVITAS
------------------------------------------------------------------------------

Geometri uji (rencana testbed): saluran PVC persegi,
  lebar dasar b=100 mm, dinding vertikal (m=0), kemiringan S=0.5%, n=0.010

  tinggi air   A(m2)      R(m)     v(m/s)    Q(L/menit)
  --------------------------------------------------------
       5 mm    0.00050   0.00455    0.1940        5.82
      10 mm    0.00100   0.00833    0.2906       17.44
      15 mm    0.00150   0.01154    0.3611       32.50
      20 mm    0.00200   0.01429    0.4163       49.96
      25 mm    0.00250   0.01667    0.4614       69.21
      30 mm    0.00300   0.01875    0.4991       89.83
      40 mm    0.00400   0.02222    0.5589      134.14

  PRESYARATAN VALIDITAS (penting untuk rancangan testbed):
  Manning berlaku untuk aliran gravitasi seragam (uniform steady flow).
  Bila saluran uji dialiri pompa langsung, debit ditentukan kurva pompa,
  bukan gravitasi -> Manning TIDAK valid diuji di sana.
  Karena itu testbed WAJIB gravity-fed: pompa -> tangki header ->
  saluran mengalir gravitasi -> tangki penampung -> kembali ke pompa.

------------------------------------------------------------------------------
BAGIAN 8 — CRC8 PAKET LoRa (vektor uji untuk firmware)
------------------------------------------------------------------------------

  Uji known-answer (standar CRC-8/ATM, polynomial 0x07, init 0x00):
    crc8('123456789') = 0xF4  BENAR (nilai referensi 0xF4)

  Vektor uji paket:
  --------------------------------------------------
  01 00 50 00                              0x1A
  02 07 ff 00                              0xED
  03 ff 00 01                              0x16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 0x00

  Angka-angka ini dipakai sebagai unit test di firmware C++ (harus sama).

==============================================================================
KESIMPULAN VALIDASI
==============================================================================

  1. MF hujan v2 lolos uji balik terhadap data BMKG nyata: noise floor
     tidak memicu alarm, hujan lebat mendorong status ke SIAGA.
  2. Bug input mati pada MF lama terbukti kuantitatif (selisih risk = 0.00).
  2b. Risiko jenuh pada kasus ekstrem teridentifikasi (level 100% -> risk 40)
      dan ditambal dengan lapisan keselamatan eksplisit 3 aturan.
  3. Tabel 27 kombinasi Layer 2 lengkap dan konsisten (27 baris, 1 AMAN).
  4. Lead time dari prakiraan hujan terkuantifikasi: 130 menit (hujan lebat).
  5. Estimasi waktu meluap least-squares: galat 0.000 menit pada kasus uji bersih.
  6. Semua angka di atas dapat direproduksi: python run_validasi.py

  STATUS: validasi tingkat model SELESAI. Validasi hardware & testbed = Fase 3-4.
```
