// ═══════════════════════════════════════════════════════════
// SERVER DASHBOARD — Sistem Prediksi Banjir
// Menerima data dari ESP32, menyediakan API untuk dashboard
// ═══════════════════════════════════════════════════════════

const express = require('express');
const path = require('path');
const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// CORS manual (tanpa perlu install package cors)
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  if (req.method === 'OPTIONS') return res.sendStatus(200);
  next();
});

// ── Penyimpanan data di memory (cukup untuk MVP/demo) ──
let dataTerkini = {
  tinggi_air_persen: 0,
  debit_lpm: 0,
  risk_level: 0,
  status: "AMAN",
  estimasi_meluap: "-",
  curah_hujan: 0,
  kondisi_cuaca: "-",
  suhu: 0,
  kelembapan: 0,
  tutupan_awan: 0,
  kecepatan_angin: 0,
  arah_angin: "-",
  lokasi_desa: "-",
  lokasi_kecamatan: "-",
  waktu_bmkg: "-",
  last_update: null
};

// Histori untuk grafik (simpan 100 data terakhir)
let historiData = [];
const MAX_HISTORI = 100;

// ═══════════════════════════════════════
// ENDPOINT — ESP32 kirim data ke sini
// ═══════════════════════════════════════
app.post('/api/data', (req, res) => {
  const data = req.body;

  dataTerkini = {
    ...dataTerkini,
    ...data,
    last_update: new Date().toISOString()
  };

  // Simpan ke histori untuk grafik
  historiData.push({
    waktu: new Date().toISOString(),
    tinggi_air_persen: data.tinggi_air_persen || 0,
    debit_lpm: data.debit_lpm || 0,
    risk_level: data.risk_level || 0
  });

  if (historiData.length > MAX_HISTORI) {
    historiData.shift(); // buang data paling lama
  }

  console.log(`[${new Date().toLocaleTimeString()}] Data diterima — Risk: ${data.risk_level}%, Status: ${data.status}`);
  res.json({ success: true, message: 'Data diterima' });
});

// ═══════════════════════════════════════
// ENDPOINT — Dashboard ambil data terkini
// ═══════════════════════════════════════
app.get('/api/data', (req, res) => {
  res.json(dataTerkini);
});

// ═══════════════════════════════════════
// ENDPOINT — Dashboard ambil histori untuk grafik
// ═══════════════════════════════════════
app.get('/api/histori', (req, res) => {
  res.json(historiData);
});

// ═══════════════════════════════════════
// ENDPOINT — Cek server hidup
// ═══════════════════════════════════════
app.get('/api/status', (req, res) => {
  res.json({
    server: 'online',
    total_data_diterima: historiData.length,
    last_update: dataTerkini.last_update
  });
});

app.listen(PORT, () => {
  console.log(`Server berjalan di port ${PORT}`);
});
