# FloodGuard Village (FGV) — Sistem Prediksi Dini Banjir

Repositori teknis **FloodGuard Village**, prototipe sistem prediksi dini banjir 3 node sensor,
LoRa 433 MHz, inferensi Fuzzy Sugeno 2-layer, dan dashboard real-time.

> **Batas kejujuran:** artefak di repo ini bercampur antara *rancangan*, *simulasi*, dan *pengukuran*.
> Lihat "Metrik kejujuran" untuk status per fitur. Jangan anggap semua yang tertulis di sini
> sebagai hasil uji lapangan.

## Arsitektur singkat

- **Node 1/2/3** (hulu-tengah-hilir): sensor ultrasonik JSN-SR04T + Adaptive Flow Engine (Manning),
  menjalankan Layer 1 Fuzzy Sugeno 27 rule → output `risk 0..100`.
- **Gateway ESP32-S3**: LoRa RX kontinu, timestamp NTP, Layer 2 pattern classifier (27 kombinasi),
  alert engine, push Firebase (opsional), UI TFT ST7735.
- **Dashboard**: server Node.js in-memory (`dashboard/fgv-dashboard/server.js`) + web
  (`public/index.html`); versi MVP lama ada di `dashboard/flood-dashboard-server`.
- **Sumber hujan**: prakiraan BMKG mm/3 jam. **Belum ada sensor hujan fisik.**
- Paket LoRa FGV: 18 byte + CRC, sync word `0xF1` (sengaja beda dengan proyek lain).

## Mulai cepat

Validasi model:

```bash
cd sim
python run_validasi.py
```

Rincian ada di `sim/HASIL_VALIDASI.md`.

Dashboard lokal:

```bash
cd dashboard/fgv-dashboard
node server.js
# http://localhost:3000
```

Contoh kirim data dummy ada di `dashboard/flood-dashboard-server/README.md`.

## Flash firmware

1. Arduino IDE 2.x + board package `esp32 by Espressif`.
2. Library: `LoRa`, `Adafruit GFX`, `Adafruit ST7735`, `ArduinoJson`.
3. Salin `config/secrets.example.h` → `secrets.h` di folder firmware yang dipakai, isi sendiri
   (jangan commit file berisi nilai nyata).
4. Isi WiFi SSID/PASS, Firebase host (opsional), `BMKG_ADM4`, lalu `Q_REF[]` + `CHAN_MM` sesuai kalibrasi.
5. Board: node WROOM/DevKit V1 = `esp32:esp32:esp32`; node S3 + gateway = `esp32s3`.
6. Upload lewat USB; pastikan Serial Monitor tidak menahan port.

Pin map & format paket: `docs/ARSITEKTUR_DAN_PROTOKOL.md`.

## Metrik kejujuran

| Fitur / angka | Status sebenarnya | Bukti |
|---|---|---|
| Layer 1 Fuzzy 27 rule | disimulasikan (Python) | `sim/fgv_engine.py` |
| Layer 2 Pattern 27 kombinasi | disimulasikan (Python) | `sim/run_validasi.py` |
| Manning debit | uji telemetri terbatas; FS400A opsional | `hardware_poc/poc5_manning` |
| Sumber hujan | prakiraan BMKG, bukan sensor | `firmware/gateway_fgv_v4/*.ino` |
| Lag hulu–hilir | rancangan; gateway timestamp | `docs/ARSITEKTUR_DAN_PROTOKOL.md` |
| Q_REF / CHAN_MM | nilai default, belum kalibrasi lapangan | konstanta firmware |
| LoRa AES | tidak ada; hanya sync word | `RF_SYNCWORD = 0xF1` |
| State dashboard | in-memory; restart = hilang | `dashboard/fgv-dashboard/server.js` |

## Firebase (opsional)

Repo ini **tidak menyertakan kredensial maupun rules produksi**. Contoh rules yang dikirim
adalah template *deny-by-default*:

```json
{ "rules": { "fgv": { ".read": false, ".write": false } } }
```

Untuk deployment nyata, atur sendiri: beri akses `write` untuk gateway dan `read` untuk
dashboard sesuai model autentikasi yang dipilih. Jangan menaruh token atau service account
di repo ini.

## Anonimisasi

Package ini sudah disanitasi:
- tidak ada nama personal, nomor telepon, email, atau path mesin pengembang;
- WiFi SSID/password, host Firebase, dan kode lokasi BMKG diganti placeholder;
- rules Firebase produksi diganti template aman;
- dokumen internal (proposal, makalah, slide, RAB, kwitansi, foto/video) tidak disertakan.

## Regenerasi package

`BUILD_SHARE.example.py` adalah **template generik**. Generator asli yang memuat pemetaan
anonimisasi project disimpan di luar repo publik agar pemetaan itu tidak ikut terbit.

```bash
FGV_SRC="/path/ke/proyek" FGV_OUT="/path/ke/github_share" python BUILD_SHARE.example.py
```

Verifikasi integritas paket:

```bash
python - <<'PY'
import json, hashlib
from pathlib import Path
m=json.load(open('MANIFEST.json'))
for e in m['files']:
    h=hashlib.sha256(Path(e['path']).read_bytes()).hexdigest()
    print(('OK  ' if h==e['sha256'] else 'BEDA'), e['path'])
PY
```

## Lisensi

MIT — lihat `LICENSE`. Sumber kebenaran teknis: `docs/ARSITEKTUR_DAN_PROTOKOL.md`.