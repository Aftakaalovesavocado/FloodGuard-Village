// ═══════════════════════════════════════════════════════════════
// FGV DASHBOARD SERVER (Tahap 3) — 3 node + Layer 1 + Layer 2
// ═══════════════════════════════════════════════════════════════
// Menerima POST /api/data dari gateway_fgv_v3 (JSON), menyimpan state
// 3 node + evaluasi terakhir, menyediakan GET /api/data & /api/histori
// untuk dashboard. Tanpa dependensi eksternal (Node >= 18, http bawaan)
// supaya tidak perlu npm install.
//
// Jalankan:  node server.js   →  http://localhost:3000
// ═══════════════════════════════════════════════════════════════
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 3000;
const MAX_HISTORI = 600; // hingga 200 titik per node jika semua node online

// ── State 3 node + evaluasi global ──
let dataTerkini = {
  nodes: {
    1: { id: 1, nama: "HULU", online: false, level_mm: null, q_lpm: null, v_cms: null, meluap: false, risk: null, status: "-", lat: null, lon: null, gps_fix: false, flags: null, seq: null, rssi_dbm: null, snr_db: null, iq: null, q_ref_lpm: null, chan_mm: 91, level_pct: null, last_seen: null },
    2: { id: 2, nama: "TENGAH", online: false, level_mm: null, q_lpm: null, v_cms: null, meluap: false, risk: null, status: "-", lat: null, lon: null, gps_fix: false, flags: null, seq: null, rssi_dbm: null, snr_db: null, iq: null, q_ref_lpm: null, chan_mm: 91, level_pct: null, last_seen: null },
    3: { id: 3, nama: "HILIR", online: false, level_mm: null, q_lpm: null, v_cms: null, meluap: false, risk: null, status: "-", lat: null, lon: null, gps_fix: false, flags: null, seq: null, rssi_dbm: null, snr_db: null, iq: null, q_ref_lpm: null, chan_mm: 91, level_pct: null, last_seen: null },
  },
  status_global: "NO_DATA",
  pola: "NO_DATA",
  pola_desc: "Belum ada node online",
  aksi: "Periksa daya/radio node",
  konfirmasi_sumbatan: null,
  lag_hulu_hilir_ms: null,
  rain_mm3h: null,
  rain_source: "UNAVAILABLE",
  bmkg_status: null,
  bmkg_tp_mm3h: null,
  siren_status: null,
  radio: { rx_ok: 0, crc_bad: 0, lost: 0, foreign: 0, pdr_pct: null, http_ok: 0, http_fail: 0, wifi_rssi_dbm: null },
  last_update: null,
};

let histori = [];   // [{t, node, level_mm, q_lpm, risk, status}]
let eventLog = [];  // perpindahan status node yang benar-benar teramati
const MAX_EVENTS = 100;

// ── Simulasi hujan (demo) ──
// Ditulis dari dashboard (POST /api/simulasi/hujan). Saat aktif, nilai ini
// meng-override rain_mm3h/rain_source pada GET /api/data DAN dipoll gateway
// dari RTDB /fgv/sim/hujan (mode hosted). Label "SIMULASI" dijaga jujur.
let simHujan = { aktif: false, rain_mm3h: 0, set_at: null };

function nowISO() { return new Date().toISOString(); }

const server = http.createServer((req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }

  // ── Gateway kirim data ──
  if (req.method === 'POST' && req.url === '/api/data') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e6) req.destroy(); });
    req.on('end', () => {
      try {
        const d = JSON.parse(body);
        // Format gateway_fgv_v3 (nested per node, dikirim tiap evaluasi):
        //   {"1":{id,invalid,level_mm,q_lpm,v_cms,meluap,risk,status,lat,lon},
        //    "2":{...},"3":{...},
        //    node, status_global, pola, pola_desc, aksi, konfirmasi, lag_ms}
        let nUpdated = 0;
        // Terima DUA bentuk kontrak: nested {"nodes":{...}} (gateway era-Firebase)
        // atau root-level {"1":{...},...} (gateway lokal lama).
        const src = (d.nodes && typeof d.nodes === "object") ? d.nodes : d;
        for (const k of ["1", "2", "3"]) {
          const p = src[k];
          if (!p || typeof p !== "object") continue;
          const n = dataTerkini.nodes[k];
          if (p.online === true && p.risk != null && n.status !== "-" && p.status && n.status !== p.status) {
            eventLog.push({
              t: nowISO(), node: Number(k), nama: n.nama,
              from: n.status, to: p.status, risk: Math.round(p.risk),
              lat: p.lat ?? n.lat, lon: p.lon ?? n.lon,
            });
            if (eventLog.length > MAX_EVENTS) eventLog.shift();
          }
          n.online = p.online === true;
          if (n.online) {
            n.last_seen = nowISO();
            n.seq = p.seq ?? n.seq;
            n.flags = p.flags ?? n.flags;
            n.gps_fix = p.gps_fix === true;
            n.rssi_dbm = p.rssi_dbm ?? n.rssi_dbm;
            n.snr_db = p.snr_db ?? n.snr_db;
            n.iq = p.iq ?? n.iq;
            n.q_ref_lpm = p.q_ref_lpm ?? n.q_ref_lpm;
            n.chan_mm = p.chan_mm ?? n.chan_mm;
            n.level_pct = p.level_pct ?? n.level_pct;
            if (p.gps_fix === true && p.lat != null && p.lon != null) { n.lat = p.lat; n.lon = p.lon; }
            else if (p.gps_fix === false) { n.lat = null; n.lon = null; }
          }
          if (!n.online) {
            // Snapshot gateway includes default LastEval for unseen/offline nodes; do not render those defaults as real telemetry.
          } else if (p.invalid) { n.level_mm = null; n.q_lpm = null; n.v_cms = null; n.meluap = false; n.risk = null; n.status = "-"; }
          else {
            n.level_mm = p.level_mm ?? null;
            n.q_lpm = p.q_lpm ?? null;
            n.v_cms = p.v_cms ?? null;
            n.meluap = !!p.meluap;
            if (p.risk != null) { n.risk = Math.round(p.risk); n.status = p.status || "-"; }
          }
          if (p.lat != null && p.lon != null) { n.lat = p.lat; n.lon = p.lon; }  // GPS pin-point
          nUpdated++;
        }
        if (nUpdated) {
          dataTerkini.status_global = d.status_global || dataTerkini.status_global;
          dataTerkini.rain_mm3h = d.rain_mm3h ?? null;
          dataTerkini.rain_source = d.rain_source || "UNAVAILABLE";
          dataTerkini.bmkg_status = d.bmkg_status ?? null;
          dataTerkini.bmkg_tp_mm3h = d.bmkg_tp_mm3h ?? null;
          dataTerkini.siren_status = d.siren_status ?? null;
          if (d.radio && typeof d.radio === "object") dataTerkini.radio = { ...dataTerkini.radio, ...d.radio };
          if (d.pola) {
            dataTerkini.pola = d.pola;
            dataTerkini.pola_desc = d.pola_desc || "";
            dataTerkini.aksi = d.aksi || "";
            dataTerkini.konfirmasi_sumbatan = d.konfirmasi ?? null;
          }
          if (d.lag_ms != null) dataTerkini.lag_hulu_hilir_ms = d.lag_ms;
          dataTerkini.last_update = nowISO();

          for (const k of ["1", "2", "3"]) {
            const n = dataTerkini.nodes[k];
            if (!n.online) continue;
            histori.push({ t: dataTerkini.last_update, node: Number(k), level_mm: n.level_mm, q_lpm: n.q_lpm, risk: n.risk, status: n.status });
          }
          if (histori.length > MAX_HISTORI) histori.splice(0, histori.length - MAX_HISTORI);
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: true }));
        console.log(`[${new Date().toLocaleTimeString()}] ${nUpdated} node — global ${dataTerkini.status_global}, pola ${dataTerkini.pola}`);
      } catch (e) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: false, error: 'bad json' }));
      }
    });
    return;
  }

  // ── Simulasi hujan: set/get (dashboard) ──
  if (req.method === 'POST' && req.url === '/api/simulasi/hujan') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
    req.on('end', () => {
      try {
        const d = JSON.parse(body);
        const aktif = d.aktif === true;
        let rain = Number(d.rain_mm3h);
        if (!Number.isFinite(rain) || rain < 0) rain = 0;
        if (rain > 100) rain = 100;   // pengaman; slider UI cuma sampai 50
        simHujan = { aktif, rain_mm3h: rain, set_at: nowISO() };
        console.log(`[SIM] hujan ${aktif ? 'AKTIF' : 'MATI'} @ ${rain.toFixed(1)} mm/3jam`);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: true, ...simHujan }));
      } catch (e) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: false, error: 'bad json' }));
      }
    });
    return;
  }
  if (req.method === 'GET' && req.url === '/api/simulasi/hujan') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(simHujan));
    return;
  }

  // ── Dashboard ambil data ──
  if (req.method === 'GET' && req.url === '/api/data') {
    // tandai offline bila >10 dtk tanpa kabar (3 siklus node)
    for (const i of [1, 2, 3]) {
      const n = dataTerkini.nodes[i];
      if (n.last_seen && Date.now() - new Date(n.last_seen).getTime() > 10000) n.online = false;
    }
    // Merge sim hujan: saat aktif, override sumber gateway (tanpa mengubah state dasar)
    const payload = { ...dataTerkini };
    if (simHujan.aktif) {
      payload.rain_mm3h = simHujan.rain_mm3h;
      payload.rain_source = "SIMULASI";
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(payload));
    return;
  }

  if (req.method === 'GET' && req.url === '/api/histori') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(histori));
    return;
  }

  if (req.method === 'GET' && req.url === '/api/events') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(eventLog.slice().reverse()));
    return;
  }

  if (req.method === 'GET' && req.url === '/api/status') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ server: 'online', total: histori.length, last_update: dataTerkini.last_update }));
    return;
  }

  // ── File statis (dashboard) ──
  if (req.method === 'GET') {
    let f = req.url === '/' ? '/index.html' : req.url.split('?')[0];
    const fp = path.join(__dirname, 'public', f);
    if (fs.existsSync(fp) && fs.statSync(fp).isFile()) {
      const ext = path.extname(fp);
      const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml' }[ext] || 'application/octet-stream';
      res.writeHead(200, { 'Content-Type': mime });
      res.end(fs.readFileSync(fp));
      return;
    }
  }

  res.writeHead(404, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ error: 'not found' }));
});

server.listen(PORT, () => {
  console.log(`FGV Dashboard: http://localhost:${PORT}`);
  console.log(`POST /api/data  (gateway)  ·  GET /api/data, /api/histori (dashboard)`);
});
