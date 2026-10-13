/*
 * FGV GATEWAY v3 — Agregator 3 node + Fuzzy L1 + Pattern L2 + OLED
 * =================================================================
 * Menambah gateway_fgv_v2:
 *   - state 3 node (hulu/tengah/hilir), deteksi OFFLINE > 9 dtk
 *   - Layer 1 Fuzzy 27 rule per node  (fuzzy_layer1.h, port 1:1 engine.py)
 *   - Layer 2 Spatial Classifier      (pattern_layer2.h)
 *   - konfirmasi backwater via I_Q hilir vs baseline
 *   - lag time hulu->hilir di klok gateway
 *
 * KONFIG WAJIB (dua tabel di bawah): Q_REF per node (kalibrasi S1) dan
 * CHAN_MM per node (kapasitas kolam ukur = 91 mm, terukur 20/21 Sep).
 * Hujan: prakiraan BMKG (mm/3jam) via api.bmkg.go.id, kode desa configurable.
 * Slot pertama respons = window berjalan (terverifikasi empiris) -> tanpa NTP.
 * Sensor hujan fisik TIDAK dipasang (pending improvement) — jujur di makalah:
 * sumber hujan = PRAKIRAAN BMKG, bukan pengukuran langsung.
 *
 * Radio & pin: identik v2. Board: esp32:esp32:esp32s3. Serial: 115200.
 */

#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>    // parse respons BMKG (~8 KB utk 1 desa)
#include <stdint.h>
#include "fuzzy_layer1.h"
#include "pattern_layer2.h"

// ══════════ KONFIG WIFI & DASHBOARD ══════════
// ISI sesuai hotspot/router demo. Dashboard: jalankan `node server.js`
// di PC (fgv-dashboard/), SERVER_URL pakai IP lokal PC (bukan localhost!).
const char* WIFI_SSID  = "GANTI_DENGAN_SSID_KAMU";
const char* WIFI_PASS  = "GANTI_DENGAN_PASSWORD_KAMU";
const char* FIREBASE_URL = "https://GANTI_DENGAN_PROJECT_ID.firebaseio.com/fgv/current.json";
const uint32_t WIFI_RETRY_MS = 5000;
const uint32_t FIREBASE_PUSH_MS = 5000;   // snapshot gateway -> Firebase RTDB

// ---------------- Pin & radio ----------------
#define LORA_NSS 10
#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_RST 9
#define LORA_DIO0 8
#define OLED_SDA 4
#define OLED_SCL 5
#define OLED_ADDR 0x3C

const long   RF_FREQ_HZ = 433000000L;
const int    RF_SF = 9;
const long   RF_BW = 125E3;
const int    RF_CR = 5;
const uint8_t RF_SYNCWORD = 0xF1;

const uint8_t  PKT_LEN = 18;
const uint16_t LEVEL_INVALID = 0xFFFF;
const uint32_t SUMMARY_MS = 30000;
const uint32_t OLED_PERIOD_MS = 500;
const uint32_t OFFLINE_MS = 9000;     // 3 siklus node (TX tiap 3 s)
const uint8_t  WDT_S = 8;
// ══════════ KONFIG BMKG — prakiraan hujan mm/3jam ══════════
// Kode desa Kemendagri (ber-titik) lokasi pemasangan. Default: Desa_Contoh, Kecamatan_Contoh, Provinsi_Contoh (lokasi pengujian) — ganti dengan kode adm4 4-level lokasi Anda. Respons 1 desa ≈ 8 KB, tanpa gzip.
const char*    BMKG_URL        = "https://api.bmkg.go.id/publik/prakiraan-cuaca?adm4=00.00.00.0000";
const uint32_t BMKG_PERIOD_MS  = 15UL * 60UL * 1000UL;   // cek 15 menit (BMKG update 3 jam)
const uint32_t BMKG_STALE_MS   = 6UL  * 3600UL * 1000UL; // >6 jam tanpa sukses = STALE

// ══════════ KONFIG SIMULASI HUJAN (demo) ══════════
// Dashboard menulis {aktif, rain_mm3h} ke node RTDB /fgv/sim/hujan.
// Gateway polling tiap SIM_PERIOD_MS; saat aktif, nilai sim MENGGANTIKAN BMKG
// sebagai input fuzzy L1 dan rain_source payload = "SIMULASI" (jujur: bukan
// data BMKG, bukan sensor fisik). Saat dimatikan, nilai BMKG dipulihkan.
const char*    SIM_URL         = "https://GANTI_DENGAN_PROJECT_ID.firebaseio.com/fgv/sim/hujan.json";
const uint32_t SIM_PERIOD_MS   = 10000;                 // polling sim tiap 10 dtk

// ══════════ KONFIG NODE — ISI DARI TESTBED ══════════
// Q_REF: debit baseline per node dari kalibrasi S1 (0 = belum kalibrasi ->
// I_Q fallback = Q L/min mentah, tetap informatif untuk demo).
float Q_REF[4]  = {0, 10.0f, 10.0f, 10.0f};   // [1]=hulu [2]=tengah [3]=hilir
// CHAN_MM: kapasitas WADAH PENGUKUR (kolam penenang hilir), bukan talang.
// Terukur 20/21 Sep: kering 331 mm, banjir 240 mm -> kolom air 91 mm.
// Skala: 20 mm -> 22% (AMAN), 55 mm -> 60% (WASPADA), >=75% (68 mm) -> BAHAYA
// (safety floor), 90 mm -> 99% (BAHAYA penuh). Verifikasi: sim/fgv_engine.py.
float CHAN_MM[4] = {0, 91.0f, 91.0f, 91.0f};  // kapasitas kolam ukur per node (mm)

struct __attribute__((packed)) FgvTx {
  uint8_t node_id, seq;
  uint16_t level_mm, q_lpm_x10, v_cms;
  uint8_t flags, crc8;
  int32_t lat_e7, lon_e7;      // GPS pin-point; 0x7FFFFFFF = belum fix
};
static_assert(sizeof(FgvTx) == 18, "FGV node packet must stay exactly 18 bytes");

struct NodeState {
  uint16_t level_mm = LEVEL_INVALID;
  uint16_t q10 = 0, v_cms = 0;
  uint8_t flags = 0, last_seq = 0;
  int8_t rssi = 0;
  float snr = 0.0f;
  int32_t lat_e7 = 0x7FFFFFFF, lon_e7 = 0x7FFFFFFF;   // GPS
  bool seen = false;
  uint32_t last_seen = 0;
};
NodeState nodes[4];                        // [1..3]
int16_t last_seq_global[4] = {-1,-1,-1,-1};

uint32_t n_ok = 0, n_crc_bad = 0, n_lost = 0, n_foreign = 0;
uint32_t t_summary = 0, t_oled = 0, t_wifi = 0, t_http = 0;
bool wifi_ok = false;
uint32_t http_ok = 0, http_fail = 0;

// ---- state BMKG (hujan prakiraan) ----
float    g_rain_mm3h = 0.0f;              // nilai yang di-feed ke fuzzy L1
float    g_rain_tp_mm3h = 0.0f;           // prakiraan sukses terakhir (payload bmkg_tp_mm3h)
char     g_bmkg_status[12] = "OFFLINE";   // OFFLINE -> AKTIF -> STALE
uint32_t g_bmkg_last_ok = 0;              // millis() sukses terakhir (0 = belum pernah)
uint32_t t_bmkg = 0;                      // timer fetch (0 = fetch segera setelah WiFi)

// ---- state simulasi hujan (demo) ----
bool     g_sim_aktif = false;             // true = nilai sim override BMKG
float    g_sim_rain_mm3h = 0.0f;          // nilai sim terakhir dari RTDB
float    g_rain_bmkg_mm3h = 0.0f;         // cache nilai BMKG (dipulihkan saat sim mati)
uint32_t t_sim = 0;                       // timer polling sim
uint32_t lag_hulu_hilir_ms = 0; bool lag_valid = false;
uint32_t t_hulu_last = 0, t_hilir_last = 0;

// snapshot evaluasi terakhir utk HTTP POST
struct LastEval {
  uint8_t node = 0; uint16_t level_mm = 0, q10 = 0, v = 0;
  bool meluap = false, invalid = false;
  float risk = 0; uint8_t status4 = 0;
};
LastEval last_eval[4];

int8_t scr_rssi = 0; float scr_snr = 0;
uint8_t scr_node = 0; uint16_t scr_level = 0, scr_q10 = 0; uint8_t scr_flags = 0;
uint8_t scr_status_global = 0; L2Kelas scr_kelas = L2_AMAN; uint8_t scr_konf = 0;
bool scr_valid = false;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oled_ok = false;

// BUG 2: status global jujur saat 0 node online. Array LOKAL gateway —
// fuzzy_layer1.h tetap port 1:1 engine (indeks 4 "NO_DATA" tidak ada di engine).
// Diletakkan SETELAH struct agar auto-prototipe Arduino tidak bergeser ke atas.
static const char *GW_STATUS_NAME[5] = { "AMAN", "WASPADA", "SIAGA", "BAHAYA", "NO_DATA" };
static inline const char *gwStatusName(uint8_t s) { return GW_STATUS_NAME[s > 4 ? 4 : s]; }

uint8_t packetCrc8(const uint8_t *packet) {
  // CRC byte is at offset 9 in packed FgvTx; omit that byte from the CRC input.
  uint8_t c = 0;
  for (uint8_t i = 0; i < sizeof(FgvTx); i++) {
    if (i == 9) continue;
    c ^= packet[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

volatile bool pkt_flag = false; volatile uint8_t pkt_len = 0;
void onReceive(int s) { pkt_len = (uint8_t)s; pkt_flag = true; }

static inline bool nodeOnline(uint8_t i) {
  return nodes[i].seen && (millis() - nodes[i].last_seen <= OFFLINE_MS);
}

// I_Q node i (fallback: L/min mentah bila Q_ref belum dikalibrasi)
static inline float iqOf(uint8_t i) {
  const float q = (nodes[i].q10 == 0xFFFF) ? 0.0f : nodes[i].q10 / 10.0f;
  return (Q_REF[i] > 0.0f) ? q / Q_REF[i] : q;
}
static inline float levelPctOf(uint8_t i) {
  if (nodes[i].level_mm == LEVEL_INVALID || CHAN_MM[i] <= 0) return 0.0f;
  return (float)nodes[i].level_mm / CHAN_MM[i] * 100.0f;
}

void evaluateLayer(uint8_t id_new) {
  // ---- Layer 1 per node (yang baru datang + recompute semua utk konsistensi)
  uint8_t l1_of[4] = {0,0,0,0};
  uint8_t status_global = 0;
  uint8_t online_count = 0;
  for (uint8_t i = 1; i <= 3; i++) if (nodeOnline(i)) online_count++;
  if (online_count == 0) {                    // BUG 2: tanpa data bukan AMAN
    scr_status_global = 4;                    // NO_DATA
    scr_kelas = L2_CAMPURAN; scr_konf = 0;    // netral; JSON/OLED menampilkan NO_DATA
    return;
  }
  for (uint8_t i = 1; i <= 3; i++) {
    if (!nodeOnline(i)) { l1_of[i] = 0; continue; }
    const float risk = f1_risk(levelPctOf(i), iqOf(i), g_rain_mm3h);
    l1_of[i] = f1_level_l1(risk);
    if (i == id_new) {
      const float risk_new = risk;   // utk print
      char buf[8]; (void)buf; (void)risk_new;
    }
    const uint8_t st4 = f1_status4(risk);
    if (st4 > status_global) status_global = st4;
  }
  scr_status_global = status_global;

  // ---- Layer 2: hanya sah bila 3 node online (data parsial = CAMPURAN jujur)
  L2Kelas kelas; uint8_t konf = 0;
  const bool all3 = nodeOnline(1) && nodeOnline(2) && nodeOnline(3);
  if (all3) l2_classify(l1_of[1], l1_of[2], l1_of[3], iqOf(3), 1.0f, &kelas, &konf);  // iqOf sudah rasio thd baseline -> basis 1.0 (bug 1: jangan Q_REF)
  else      kelas = L2_CAMPURAN;
  scr_kelas = kelas; scr_konf = konf;
}

// POST state terakhir ke dashboard (dipanggil periodik dari loop()).
// Ditulis ulang satu-statement-per-field agar semantik JSON tak ambigu:
// nilai hujan = angka JSON; penutup brace eksplisit per objek.
void postToDashboard() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure tls;
  tls.setInsecure(); // demo: TLS terenkripsi, validasi sertifikat belum dipasang
  HTTPClient http;
  http.begin(tls, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  String js = "{\"nodes\":{";
  for (uint8_t i = 1; i <= 3; i++) {
    const LastEval &e = last_eval[i];
    const NodeState &st = nodes[i];
    js += (i > 1 ? "," : "");
    js += "\"" + String(i) + "\":{";
    js += "\"id\":" + String(i);
    js += ",\"invalid\":" + String((!st.seen || e.invalid) ? "true" : "false");
    js += ",\"level_mm\":" + ((!st.seen || e.invalid) ? String("null") : String(e.level_mm));
    js += ",\"q_lpm\":" + ((!st.seen || e.invalid) ? String("null") : String(e.q10 / 10.0f, 1));
    js += ",\"v_cms\":" + ((!st.seen || e.invalid) ? String("null") : String(e.v));
    js += ",\"meluap\":" + String((st.seen && e.meluap) ? "true" : "false");
    js += ",\"risk\":" + ((!st.seen || e.invalid) ? String("null") : String(e.risk, 0));
    js += ",\"status\":\"" + ((!st.seen || e.invalid) ? String("-") : String(F1_STATUS_NAME[e.status4])) + "\"";
    js += ",\"online\":" + String(nodeOnline(i) ? "true" : "false");
    js += ",\"gps_fix\":" + String((st.flags & 0x04) ? "true" : "false");
    js += ",\"lat\":" + (((st.flags & 0x04) && st.lat_e7 != 0x7FFFFFFF) ? String(st.lat_e7 / 1e7, 6) : String("null"));
    js += ",\"lon\":" + (((st.flags & 0x04) && st.lon_e7 != 0x7FFFFFFF) ? String(st.lon_e7 / 1e7, 6) : String("null"));
    js += ",\"flags\":" + String(st.flags);
    js += ",\"seq\":" + String(st.last_seq);
    js += ",\"rssi_dbm\":" + String((int)st.rssi);
    js += ",\"snr_db\":" + String(st.snr, 1);
    js += ",\"iq\":" + String(iqOf(i), 3);
    js += ",\"q_ref_lpm\":" + String(Q_REF[i], 1);
    js += ",\"chan_mm\":" + String(CHAN_MM[i], 1);
    js += ",\"level_pct\":" + String(levelPctOf(i), 1);
    js += ",\"rain_mm3h\":" + String(g_rain_mm3h, 1);
    js += ",\"rain_source\":\"" + String(g_sim_aktif ? "SIMULASI" : String("BMKG_") + g_bmkg_status) + "\"";
    js += "}";
  }
  js += "},\"node\":" + String(last_eval[0].node ? last_eval[0].node : 1);
  js += ",\"gateway_uptime_ms\":" + String(millis());
  js += ",\"rain_mm3h\":" + String(g_rain_mm3h, 1);
  js += ",\"rain_source\":\"" + String(g_sim_aktif ? "SIMULASI" : String("BMKG_") + g_bmkg_status) + "\"";
  js += ",\"bmkg_status\":\"" + String(g_bmkg_status) + "\"";
  js += ",\"bmkg_tp_mm3h\":" + String(g_rain_tp_mm3h, 1);
  js += ",\"status_global\":\"" + String(gwStatusName(scr_status_global)) + "\"";
  const bool no_data = (scr_status_global == 4);   // BUG 2: tanpa data, pola juga NO_DATA
  js += ",\"pola\":\"" + String(no_data ? "NO_DATA" : L2_KELAS_NAME[scr_kelas]) + "\"";
  js += ",\"pola_desc\":\"" + String(no_data ? "Belum ada node online" : L2_KELAS_DESC[scr_kelas]) + "\"";
  js += ",\"aksi\":\"" + String(no_data ? "Periksa daya/radio node" : L2_AKSI[scr_kelas]) + "\"";
  js += ",\"konfirmasi\":" + String(scr_konf);
  if (lag_valid) js += ",\"lag_ms\":" + String(lag_hulu_hilir_ms);
  const uint32_t radio_total = n_ok + n_crc_bad + n_lost + n_foreign;
  js += ",\"radio\":{\"rx_ok\":" + String(n_ok) +
        ",\"crc_bad\":" + String(n_crc_bad) +
        ",\"lost\":" + String(n_lost) +
        ",\"foreign\":" + String(n_foreign) +
        ",\"pdr_pct\":" + String(radio_total ? (n_ok * 100.0f / radio_total) : 0.0f, 1) +
        ",\"http_ok\":" + String(http_ok) +
        ",\"http_fail\":" + String(http_fail) +
        ",\"wifi_rssi_dbm\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + "}";
  js += "}";

  const int code = http.PUT(js);
  if (code == 200) {
    http_ok++;
    Serial.print(F("[FIREBASE] PUT OK HTTP 200 bytes=")); Serial.print(js.length());
    Serial.print(F(" uptime_ms=")); Serial.println(millis());
  } else {
    http_fail++;
    Serial.print(F("[FIREBASE] PUT FAIL code=")); Serial.print(code);
    if (code > 0) {
      Serial.print(F(" body="));
      String response = http.getString();
      if (response.length() > 240) response.remove(240);
      Serial.println(response);
    } else {
      Serial.print(F(" error=")); Serial.println(http.errorToString(code));
    }
  }
  http.end();
}

// ── BMKG: prakiraan hujan (mm/3jam) ──────────────────────────────────────
// Respons: data[0].cuaca = array hari, tiap hari array slot 3-jam. Slot
// PERTAMA respons terbukti = window berjalan/berikutnya (cek empiris 30 Sep
// 2026: fetch 19:56 WIB -> slot pertama local_datetime 20:00) -> tanpa NTP.
void bmkgMaintain() {
  if (g_sim_aktif) return;              // sim aktif: jangan turunkan BMKG ke STALE
  // Usang: sukses terakhir melewati ambang -> fallback 0 + tandai STALE (jujur).
  if (g_bmkg_last_ok != 0 && millis() - g_bmkg_last_ok > BMKG_STALE_MS) {
    if (g_bmkg_status[0] != 'S') {   // belum ditandai STALE
      g_rain_bmkg_mm3h = 0.0f;
      if (!g_sim_aktif) g_rain_mm3h = 0.0f;
      strlcpy(g_bmkg_status, "STALE", sizeof(g_bmkg_status));
      Serial.println(F("[BMKG] data usang (>6 jam) -> fallback 0.0, status STALE"));
    }
  }
}

void fetchBMKG() {
  if (WiFi.status() != WL_CONNECTED) return;
  esp_task_wdt_reset();                 // HTTPS+TLS bisa makan waktu; jaga WDT
  WiFiClientSecure tls;
  tls.setInsecure();                    // demo: terenkripsi, tanpa validasi CA
  HTTPClient http;
  http.begin(tls, BMKG_URL);
  http.setTimeout(6000);
  const int code = http.GET();
  esp_task_wdt_reset();
  bool parsed = false;
  if (code == 200) {
    const String body = http.getString();   // ~8 KB utk 1 desa
    esp_task_wdt_reset();
    JsonDocument doc;                       // ArduinoJson 7 (elastis)
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      const JsonObject loc = doc["lokasi"];
      const JsonArray slots = doc["data"][0]["cuaca"][0];
      if (!slots.isNull() && slots.size() > 0) {
        const float tp = slots[0]["tp"] | 0.0f;
        g_rain_tp_mm3h = tp;
        g_rain_bmkg_mm3h = tp;
        if (!g_sim_aktif) g_rain_mm3h = tp;   // sim aktif: BMKG hanya di-cache
        g_bmkg_last_ok = millis();
        strlcpy(g_bmkg_status, "AKTIF", sizeof(g_bmkg_status));
        parsed = true;
        Serial.print(F("[BMKG] OK "));
        Serial.print((const char*)(loc["desa"] | (const char*)"-"));
        Serial.print(F(" tp=")); Serial.print(tp, 1);
        Serial.print(F(" mm/3jam ("));
        Serial.print((const char*)(slots[0]["weather_desc"] | (const char*)"-"));
        Serial.println(F(")"));
      }
    }
  }
  if (!parsed) {
    Serial.print(F("[BMKG] FAIL code=")); Serial.println(code);
    // nilai lama tetap dipakai sampai ambang STALE (bmkgMaintain yang menurunkan)
  }
  http.end();
}

// ── Simulasi hujan (demo): polling override dari dashboard ────────────────
// Node RTDB /fgv/sim/hujan: {"aktif":true,"rain_mm3h":12.5} atau {"aktif":false}
// atau null (belum pernah di-set). Sim aktif = input fuzzy L1 + label SIMULASI.
void pollSimHujan() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.begin(tls, SIM_URL);
  http.setTimeout(4000);
  const int code = http.GET();
  bool applied = false;
  if (code == 200) {
    const String body = http.getString();     // kecil (<100 byte)
    if (body == "null") { applied = true; }   // node RTDB belum ada = sim tidak dikonfigurasi
    else {
    JsonDocument doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok && !doc.isNull()) {
      const bool aktif = doc["aktif"] | false;
      const float rain = doc["rain_mm3h"] | 0.0f;
      if (aktif != g_sim_aktif) {
        if (aktif) {
          Serial.print(F("[SIM] AKTIF ")); Serial.print(rain, 1);
          Serial.println(F(" mm/3jam (override BMKG, input fuzzy L1)"));
        } else {
          Serial.println(F("[SIM] MATI -> pulihkan nilai BMKG"));
          g_rain_mm3h = g_rain_bmkg_mm3h;
          t_bmkg = 0;                          // fetch BMKG segera di loop()
        }
      }
      g_sim_aktif = aktif;
      g_sim_rain_mm3h = rain;
      if (aktif && rain != g_rain_mm3h) {
        g_rain_mm3h = rain;                    // slider digeser saat aktif
        Serial.print(F("[SIM] nilai -> ")); Serial.print(rain, 1); Serial.println(F(" mm/3jam"));
      }
      applied = true;
    }
    }
  }
  if (!applied) { Serial.print(F("[SIM] polling gagal code=")); Serial.println(code); }
  http.end();
}

void handlePacket(const FgvTx &p, int8_t rssi, float snr) {
  NodeState &st = nodes[p.node_id];
  if (st.seen) {
    const uint8_t gap = (uint8_t)((p.seq - st.last_seq) & 0xFF);
    if (gap > 1 && gap < 128) n_lost += (gap - 1);
  }
  st.last_seq = p.seq; st.seen = true; st.last_seen = millis();
  st.level_mm = p.level_mm; st.q10 = p.q_lpm_x10; st.v_cms = p.v_cms; st.flags = p.flags;
  st.rssi = rssi; st.snr = snr;
  st.lat_e7 = p.lat_e7; st.lon_e7 = p.lon_e7;
  n_ok++;
  if (p.node_id == 1) { t_hulu_last = millis();  lag_valid = false; }
  if (p.node_id == 3 && t_hulu_last > 0) { t_hilir_last = millis(); lag_hulu_hilir_ms = t_hilir_last - t_hulu_last; lag_valid = true; }

  evaluateLayer(p.node_id);

  // snapshot utk dashboard
  LastEval &e = last_eval[p.node_id];
  e.node = p.node_id;
  e.invalid = (p.level_mm == LEVEL_INVALID);
  e.level_mm = p.level_mm; e.q10 = p.q_lpm_x10; e.v = p.v_cms;
  e.meluap = (p.flags & 0x02);
  if (!e.invalid) {
    const float risk = f1_risk(levelPctOf(p.node_id), iqOf(p.node_id), g_rain_mm3h);
    e.risk = risk; e.status4 = f1_status4(risk);
  }
  last_eval[0] = e;   // "terakhir" utk field global dashboard

  // ---- Serial report ----
  Serial.print(F("NODE ")); Serial.print(p.node_id);
  Serial.print(F(" seq=")); Serial.print(p.seq);
  if (p.level_mm == LEVEL_INVALID) Serial.print(F("  level=INVALID"));
  else {
    Serial.print(F("  lvl=")); Serial.print(p.level_mm); Serial.print(F("mm"));
    Serial.print(F("  Q=")); Serial.print(p.q_lpm_x10 / 10.0f, 1); Serial.print(F("L/m"));
    if (p.flags & 0x02) Serial.print(F(" MELUAP"));
    Serial.print(F("  GPS:"));
    Serial.print((p.flags & 0x04) ? F("fix") : F("-"));
  }
  Serial.print(F("  risk="));
  if (p.level_mm == LEVEL_INVALID) Serial.print(F("-"));
  else {
    const float risk = f1_risk(levelPctOf(p.node_id), iqOf(p.node_id), g_rain_mm3h);
    Serial.print(risk, 0);
    Serial.print(F("(")); Serial.print(F1_STATUS_NAME[f1_status4(risk)]); Serial.print(F(")"));
  }
  Serial.print(F("  RSSI=")); Serial.print(rssi);
  Serial.print(F(" SNR=")); Serial.print(snr, 1);
  Serial.println();

  Serial.print(F("  >> GLOBAL: ")); Serial.print(gwStatusName(scr_status_global));
  Serial.print(F(" | POLA: ")); Serial.print(L2_KELAS_NAME[scr_kelas]);
  if (scr_kelas == L2_BACKWATER) {
    Serial.print(F(" ("));
    if (scr_konf == 1)      Serial.print(F("sumbatan KUAT"));
    else if (scr_konf == 2) Serial.print(F("debit stagnan"));
    else if (scr_konf == 3) Serial.print(F("debit naik"));
    else                    Serial.print(F("tanpa info I_Q"));
    Serial.print(F(")"));
  }
  Serial.print(F(" | AKSI: ")); Serial.print(L2_AKSI[scr_kelas]);
  if (lag_valid && scr_kelas == L2_BACKWATER) {
    Serial.print(F(" | lag hulu-hilir: ")); Serial.print(lag_hulu_hilir_ms); Serial.print(F(" ms"));
  }
  Serial.println();
}

void drawOLED() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0, 0);
  display.println(F("FGV GW v3"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print(F("ST: "));
  if (scr_valid) display.print(gwStatusName(scr_status_global));   // BUG 2: bisa NO_DATA
  else           display.print(F("...."));

  display.setCursor(70, 14);
  if (scr_status_global == 4) display.print(F("NO_DATA"));         // BUG 2
  else                        display.print(L2_KELAS_NAME[scr_kelas]);

  display.setCursor(0, 24);
  for (uint8_t i = 1; i <= 3; i++) {          // N1 N2 N3: o/offline . rendah m sedang H tinggi
    display.print(F("N")); display.print(i); display.print(F(":"));
    if (!nodeOnline(i)) display.print(F("-- "));
    else if (nodes[i].level_mm == LEVEL_INVALID) display.print(F("?? "));
    else {
      const float pct = levelPctOf(i);
      display.print(nodes[i].level_mm); display.print(pct >= 75.0f ? F("! ") : F("  "));
    }
  }

  display.setCursor(0, 34);
  if (scr_valid) {
    display.print(F("t")); display.print(scr_node); display.print(F(" "));
    if (scr_level == LEVEL_INVALID) display.print(F("INV"));
    else { display.print(scr_level); display.print(F("mm Q")); display.print(scr_q10 / 10.0f, 1); }
  } else display.print(F("menunggu..."));

  display.setCursor(0, 44);
  if (scr_kelas == L2_BACKWATER && lag_valid) {
    display.print(F("lag ")); display.print(lag_hulu_hilir_ms); display.print(F("ms"));
  } else {
    display.print(F("RSSI ")); display.print(scr_rssi);
  }

  display.setCursor(0, 54);
  display.print(F("RX=")); display.print(n_ok);
  display.print(F(" lost=")); display.print(n_lost);
  const uint32_t tot = n_ok + n_crc_bad + n_lost + n_foreign;
  if (tot) { display.print(F(" PDR=")); display.print(n_ok * 100.0f / tot, 0); display.print(F("%")); }
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("FGV GATEWAY v3 | 3-node + Fuzzy L1 + Pattern L2"));

  esp_task_wdt_config_t cfg = { .timeout_ms = (uint32_t)WDT_S * 1000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_deinit();
  esp_task_wdt_init(&cfg);
  esp_task_wdt_add(NULL);
  Serial.println(F("Watchdog aktif"));

  Wire.begin(OLED_SDA, OLED_SCL);
  oled_ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oled_ok) Serial.println(F("WARN: OLED tidak terdeteksi. Lanjut tanpa layar."));
  else drawOLED();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(RF_FREQ_HZ)) { Serial.println(F("FATAL: LoRa.begin gagal.")); while (true) delay(1000); }
  LoRa.setSpreadingFactor(RF_SF);
  LoRa.setSignalBandwidth(RF_BW);
  LoRa.setCodingRate4(RF_CR);
  LoRa.setSyncWord(RF_SYNCWORD);
  LoRa.enableCrc();
  LoRa.onReceive(onReceive);
  LoRa.receive();
  Serial.println(F("LoRa OK. Menunggu node 1/2/3..."));
  t_summary = t_oled = t_wifi = t_http = millis();
}

void wifiMaintain() {
  const bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !wifi_ok) {
    wifi_ok = true;
    Serial.print(F("[WIFI] terhubung, IP: "));
    Serial.println(WiFi.localIP());
  }
  if (!connected) {
    wifi_ok = false;
    if (millis() - t_wifi >= WIFI_RETRY_MS) {
      t_wifi = millis();
      Serial.print(F("[WIFI] menyambung ke "));
      Serial.println(WIFI_SSID);
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
}

void loop() {
  if (pkt_flag) {
    noInterrupts(); pkt_flag = false; const uint8_t size = pkt_len; interrupts();
    const int8_t rssi = LoRa.packetRssi();
    const float snr = LoRa.packetSnr();
    scr_rssi = rssi; scr_snr = snr;

    if (size != PKT_LEN) {
      n_foreign++;
      Serial.print(F("RX asing len=")); Serial.print(size); Serial.print(F(" hex:"));
      while (LoRa.available()) { Serial.print(F(" ")); Serial.print(LoRa.read(), HEX); }
      Serial.println();
    } else {
      FgvTx p;
      LoRa.readBytes((uint8_t *)&p, sizeof(p));
      const bool crc_ok = (p.crc8 == packetCrc8((uint8_t *)&p));
      const bool id_ok = (p.node_id >= 1 && p.node_id <= 3);
      if (crc_ok && id_ok) {
        handlePacket(p, rssi, snr);
        scr_valid = true; scr_node = p.node_id; scr_level = p.level_mm; scr_q10 = p.q_lpm_x10; scr_flags = p.flags;
      } else {
        n_crc_bad++;
        Serial.print(F("RX REJECT len=")); Serial.print(size);
        Serial.print(F(" id=")); Serial.print(p.node_id);
        Serial.print(F(" id_ok=")); Serial.print(id_ok ? 1 : 0);
        Serial.print(F(" crc_rx=0x")); Serial.print(p.crc8, HEX);
        Serial.print(F(" crc_calc=0x")); Serial.print(packetCrc8((uint8_t *)&p), HEX);
        Serial.print(F(" seq=")); Serial.print(p.seq);
        Serial.print(F(" flags=0x")); Serial.print(p.flags, HEX);
        Serial.print(F(" bytes="));
        for (uint8_t j = 0; j < sizeof(p); j++) { if (j) Serial.print(' '); if (((uint8_t *)&p)[j] < 16) Serial.print('0'); Serial.print(((uint8_t *)&p)[j], HEX); }
        Serial.println();
      }
    }
  }

  if (millis() - t_oled >= OLED_PERIOD_MS) { t_oled = millis(); evaluateLayer(0); if (oled_ok) drawOLED(); }  // BUG 3: re-evaluasi periodik (offline -> NO_DATA, parsial -> CAMPURAN)

  wifiMaintain();
  if (wifi_ok && millis() - t_sim >= SIM_PERIOD_MS) { t_sim = millis(); pollSimHujan(); }
  if (wifi_ok && !g_sim_aktif && (t_bmkg == 0 || millis() - t_bmkg >= BMKG_PERIOD_MS)) { t_bmkg = millis(); fetchBMKG(); }
  bmkgMaintain();
  // Push tanpa syarat scr_valid: uptime, radio counters, dan status BMKG justru
  // paling relevan saat 0 node online; dashboard menampilkan NO_DATA secara jujur.
  if (wifi_ok && millis() - t_http >= FIREBASE_PUSH_MS) {
    t_http = millis();
    postToDashboard();
  }

  if (millis() - t_summary >= SUMMARY_MS) {
    t_summary = millis();
    const uint32_t tot = n_ok + n_crc_bad + n_lost + n_foreign;
    if (!tot) { Serial.println(F("SUMMARY: belum ada paket")); }
    else {
      Serial.print(F("SUMMARY ok=")); Serial.print(n_ok);
      Serial.print(F(" crc_bad=")); Serial.print(n_crc_bad);
      Serial.print(F(" lost=")); Serial.print(n_lost);
      Serial.print(F("  PDR=")); Serial.print(n_ok * 100.0f / tot, 1);
      Serial.print(F("%  http ok=")); Serial.print(http_ok);
      Serial.print(F(" fail=")); Serial.println(http_fail);
    }
  }
  esp_task_wdt_reset();
}
