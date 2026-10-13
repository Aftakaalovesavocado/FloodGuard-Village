# PoC-4 — Wiring CDM324 Doppler (+LM358) ke ESP32-S3

> **STATUS: DITUNDA (20 Sep 2026).** Timeline testbed makin ketat
> (simulasi harus selesai 1 hari) → prioritas kembali ke jalur utama
> sesuai PLAN_ROADMAP §3: **estimasi debit = Manning + JSN**, CDM324
> bukan jalur demo. Semua desain di folder ini (sketch, analyzer, skema
> amplifier 2-tingkat) tetap tersimpan sebagai rencana bench test
> "someday". Untuk makalah: CDM324 dicatat sebagai *alternatif yang
> dipertimbangkan dan dirancang front-end-nya*, tidak diukur — sesuai
> aturan "bedakan dirancang/diukur" par.5 dokumen.

Board target: `esp32:esp32:esp32s3` · Serial: 115200
STATUS: **eksperimen opsional, bukan jalur demo** (PLAN_ROADMAP §3).
Pin IF: **GPIO 7** (ADC1_CH6, sesuai ARSITEKTUR §2.1).

## Langkah 0 — identifikasi modul kamu (PENTING)

CDM324 yang jualan umum ada dua varian:

- **Varian A — modul dengan amplifier onboard** (board ungu/biru, ada
  chip kecil 8 kaki "LM358/G276" menempel di PCB, 3 pin: VCC GND IF)
  → IF-nya sudah dikuatkan. **Langsung ke Langkah A.**
- **Varian B — modul telanjang** (hanya chip radar, IF mentah ber-mV)
  → butuh LM358 lepasan. **Langkah B dulu.**

## Langkah A — modul dengan amp onboard

| CDM324 | ESP32-S3 | Catatan |
| :----- | :------- | :------ |
| VCC | VIN/5V atau 3V3 | ikut label board (umumnya 5V) |
| GND | GND | |
| IF | **GPIO 7** | langsung, tanpa pembagi |

## Langkah B — IF mentah + LM358 DUA TINGKAT (desain final)

> **KOREKSI (19 Sep):** riset menunjukkan modul telanjang CDM324 butuh
> total gain ~10.000–50.000× (referensi: duino4projects — 2 tingkat,
> gain 125,5×/tingkat, total 15.758×). Rencana "gain 100" pertama
> TERLALU KECIL — hampir pasti tidak terbaca.

### Spesifikasi modul (dari halaman produk Tokopedia, 19 Sep)

- Nama listing: "Microwave Radar Sensor Module, 24G CDM324"
- Frekuensi 24.00–24.25 GHz, output +14 dBm, beam 80°/32°, jarak ~15 m
- Suplai listing: +5V/29mA — **TAPI rail 5V ESP32-S3 kita mati** (lihat
  HASIL_POC1) → modul ditenaga dari **3V3**. Pabrikan chip (Suntec,
  CDM324D) menyebut tegangan kerja 3.3V; rating 5V pada listing umumnya
  untuk varian ber-amplifier. Status: **di luar spesifikasi listing,
  divalidasi lewat data T1** (pola sama dengan JSN di 3.3V). Fallback
  bila T1 gagal: 5V eksternal dengan GND digabung ke GND board.
- **PINOUT RESMI: Pin 1 = VCC, Pin 2 = IF, Pin 3 = GND**
- Tidak ada amplifier onboard (varian telanjang) → LM358 2-tingkat wajib
- GPIO 7 tetap aman meski ada perbedaan tegangan suplai: IF masuk lewat
  kopel 10µF — DC keblok, AC mV menumpang di bias 1.03 V

### Desain final (terverifikasi hitung)

```
Stage 1: gain 471× (Rf 470k, Rg 1k)      BW 2.1 kHz
Stage 2: gain 101× (Rf 1M,  Rg 10k)      BW 9.9 kHz
TOTAL ≈ 47.500× · band sinyal 8–320 Hz lolos semua filter
Bias = 22k atas / 10k bawah dari 3V3 → 1.03 V (tengah area linear LM358)
Filter HP input ≈ 3.9 Hz · LP anti-alias di ADC = 1k + 100nF → 1.6 kHz
```

Peran kapasitor (pertanyaan 19 Sep):
- Elco 10µF = kopel input + stabilisasi node bias (arus lambat)
- 100nF = decoupling suplai LM358 + anti-alias ADC (deraum cepat)
- Keduanya saling melengkapi, tidak saling menggantikan.

Skema breadboard baris-per-baris + prosedur uji bertahap menyusul
(setelah foto pad side terkonfirmasi + komponen datang).

## Prosedur uji T1 (hari ini)

1. Flash `poc4_doppler.ino`, buka Serial 115200.
2. **Fase diam**: sensor menghadap ruangan sepi, catat 10–15 baris
   `rms=... zc~...` → ini baseline.
3. **Fase gerak**: goyang tangan ±30 cm di depan sensor, atau arahkan ke
   kipas angin yang muter → catat lagi.
4. Vonis T1: RMS/summary gerak harus **jelas beda** dari baseline
   (mis. ≥ 3×). Beda tipis = sampel/tumbler → naikkan gain (Langkah B).
5. Kirim `d` di serial → simpan output CSV → jalankan
   `python analyze_doppler.py dump.csv` → lihat spektrum + posisi 50 Hz.

## Vonis T2 (kalau T1 lulus)

Target berputar kecepatan diketahui (kipas/CD di spindel, RPM dihitung):
`v = 2πr·RPM/60` → bandingkan dengan `f_d = 2v/λ` terukur. Kurva ini
kalibrasi Doppler — lampirkan di HASIL_POC4.

## Kriteria dokumentasi

APA PUN HASILNYA dicatat di HASIL_POC4.md — lulus ATAU gagal. Gagal
terdokumentasi tetap bernilai: makalah tinggal bilang "diuji, tidak
dipakai karena X, dengan data", persis aturan dokumen par.5.
