# MANIFEST — FloodGuard Village (FGV) share package

Ringkasan isi. Integritas per file (path, size, sha256) ada di `MANIFEST.json`.

## Folder utama

- `firmware/` — node v2 + gateway v2/v3/v4, header referensi.
- `hardware_poc/` — POC 1–5.
- `sim/` — engine Python + validasi + visualisasi.
- `dashboard/` — server baru (`fgv-dashboard`) + MVP server lama.
- `docs/` — arsitektur, protokol, kalibrasi BMKG/Manning, BOM, testbed, plan.
- `config/secrets.example.h` — template credential (placeholder).
- root — README, MANIFEST, `BUILD_SHARE.example.py`, `.gitignore`, LICENSE.

## Hasil audit anonimisasi

| Kategori | Aksi |
|---|---|
| Nama personal di firmware / license | diganti `FGV TEAM` / `FloodGuard Village Contributors` |
| WiFi SSID & password | diganti `GANTI_DENGAN_...` |
| Host Firebase | diganti `GANTI_DENGAN_PROJECT_ID...` |
| Kode lokasi BMKG | diganti `00.00.00.0000` |
| Lokasi target / institusi | diganti `Wilayah_Target` / placeholder |
| Path mesin pengembang | dihapus dari script & manifest |
| Rules Firebase | template deny-by-default, branch proyek lain dihapus |
| `.firebaserc`, cache, node_modules | tidak disertakan |

## Deliberately excluded

Proposal, jurnal, slide, CV, poster, kwitansi, cash flow, video lokasi, foto HEIC,
kredensial real, cache build, dan `share_sanitize_map.json` (pemetaan privat).

## Status honest

Firmware asli dapat dikompilasi ulang, tetapi `Q_REF`, `CHAN_MM`, WiFi, BMKG desa, dan Firebase
tetap harus diisi pengguna. Dashboard in-memory. Sumber hujan = prakiraan BMKG, bukan sensor.