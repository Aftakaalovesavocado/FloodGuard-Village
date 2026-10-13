# Sungai Watch — Dashboard Cloud

Dashboard monitoring real-time untuk Sistem Prediksi Dini Banjir.

## Isi Folder

```
flood-dashboard-server/
├── server.js          → backend Node.js (terima data ESP32 + sediakan API)
├── package.json        → daftar dependency
└── public/
    └── index.html       → tampilan dashboard
```

## Cara Deploy ke Railway (Gratis)

1. Buat akun di https://railway.app (bisa pakai GitHub login)
2. Klik "New Project" → "Deploy from GitHub repo"
   - Kalau belum ada repo GitHub, upload dulu folder ini ke GitHub baru
   - Atau pakai opsi "Empty Project" → drag & drop folder ini
3. Railway akan otomatis mendeteksi `package.json` dan menjalankan `npm start`
4. Setelah deploy selesai, klik "Settings" → "Generate Domain"
   - Kamu akan dapat URL seperti: `https://NAMA-DEPLOY-KAMU.up.railway.app`
5. Buka URL itu di browser → dashboard akan muncul (masih kosong karena ESP32 belum kirim data)

## Cara Deploy ke Render (Alternatif Gratis)

1. Buat akun di https://render.com
2. New → Web Service → hubungkan repo GitHub / upload manual
3. Build Command: `npm install`
4. Start Command: `npm start`
5. Deploy → dapat URL seperti `https://NAMA-DEPLOY-KAMU.onrender.com`

> Catatan: Render free tier akan "tidur" setelah 15 menit tanpa aktivitas, lalu perlu ~30 detik untuk bangun lagi saat diakses. Railway free tier lebih stabil untuk demo.

## Endpoint API yang Tersedia

| Method | Endpoint | Fungsi |
|---|---|---|
| POST | `/api/data` | ESP32 kirim data sensor ke sini |
| GET | `/api/data` | Dashboard ambil data terkini |
| GET | `/api/histori` | Dashboard ambil histori untuk grafik |
| GET | `/api/status` | Cek server hidup |

## Format Data yang Dikirim ESP32 (JSON)

```json
{
  "tinggi_air_persen": 42.87,
  "debit_lpm": 12.5,
  "risk_level": 35.2,
  "status": "WASPADA",
  "estimasi_meluap": "2 jam 15 menit",
  "curah_hujan": 5.2,
  "kondisi_cuaca": "Hujan Ringan",
  "suhu": 27.5,
  "kelembapan": 82,
  "tutupan_awan": 65,
  "kecepatan_angin": 8.3,
  "arah_angin": "SW",
  "lokasi_desa": "Wilayah_Target",
  "lokasi_kecamatan": "Kecamatan_Target",
  "waktu_bmkg": "2026-07-09 15:00:00"
}
```

## Testing Manual (Tanpa ESP32 Dulu)

Setelah server ter-deploy, kamu bisa test kirim data dummy pakai curl:

```bash
curl -X POST https://URL_SERVER_KAMU/api/data \
  -H "Content-Type: application/json" \
  -d '{"tinggi_air_persen": 65, "debit_lpm": 25, "risk_level": 58, "status": "SIAGA", "estimasi_meluap": "45 menit", "curah_hujan": 12, "kondisi_cuaca": "Hujan Sedang", "suhu": 26, "kelembapan": 85, "tutupan_awan": 80, "kecepatan_angin": 10, "arah_angin": "W", "lokasi_desa": "Wilayah_Target", "lokasi_kecamatan": "Kecamatan_Target"}'
```

Lalu refresh dashboard — data akan muncul.
