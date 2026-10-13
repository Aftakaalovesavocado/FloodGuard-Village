/*
 * FGV GATEWAY v4 — Receiver baru (desain HMI PALD): TFT ST7735 + tombol + buzzer
 * =============================================================================
 * v4 = v3 yang di-port ke unit receiver baru. Perubahan dari v3:
 *   - Display : OLED SSD1306 128x64 I2C (pin 4/5) -> TFT ST7735S 160x128 SPI
 *               (share bus SPI dgn LoRa; CS=6 DC=7 RST=4 BLK=5)
 *   - Input   : tombol NEXT=1 / BACK=2 (ke GND, INPUT_PULLUP) — navigasi manual
 *   - Output  : buzzer ALARM=14 (bebek 250 ms saat BAHAYA) + STATUS=15 (chirp)
 *   - UI/UX   : 5 halaman auto-rotate 8 dtk + boot checklist — pola receiver PALD
 * TIDAK berubah (sengaja): radio (433 MHz, SF9, BW125, CR5, sync 0xF1, CRC),
 * paket 18 B + CRC (skip offset 9), fuzzy L1, pattern L2, BMKG, Firebase push,
 * simulasi hujan, Q_REF/CHAN_MM.
 *
 * KONFIG WAJIB (dua tabel di bawah): Q_REF per node (kalibrasi S1) dan
 * CHAN_MM per node (kapasitas kolam ukur = 91 mm, terukur 20/21 Sep).
 * Hujan: prakiraan BMKG (mm/3jam) via api.bmkg.go.id, kode desa configurable.
 * Slot pertama respons = window berjalan (terverifikasi empiris) -> tanpa NTP.
 * Sensor hujan fisik TIDAK dipasang (pending improvement) — jujur di makalah:
 * sumber hujan = PRAKIRAAN BMKG, bukan pengukuran langsung.
 *
 * Wiring tombol/buzzer: PALD Code ESP/receiver_gateway/WIRING_BUTTONS_BUZZERS.md
 * Board: esp32:esp32:esp32s3. Serial: 115200.
 */

#include <LoRa.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
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
// TFT ST7735S 1.8" 128x160 — SPI share bus dgn LoRa (CS beda; lib pakai SPI transaction)
#define TFT_CS 6
#define TFT_DC 7
#define TFT_RST 4
#define TFT_BLK 5
// Tombol navigasi (sisi lain ke GND, INPUT_PULLUP) + buzzer via NPN (wiring PALD)
#define BTN_NEXT 1
#define BTN_BACK 2
#define BUZZER_ALARM 14    // beep 250 ms on/off saat BAHAYA
#define BUZZER_STATUS 15   // chirp transisi status + klik tombol

const long   RF_FREQ_HZ = 433000000L;
const int    RF_SF = 9;
const long   RF_BW = 125E3;
const int    RF_CR = 5;
const uint8_t RF_SYNCWORD = 0xF1;

const uint8_t  PKT_LEN = 18;
const uint16_t LEVEL_INVALID = 0xFFFF;
const uint32_t SUMMARY_MS = 30000;
const uint32_t EVAL_PERIOD_MS = 500;   // re-evaluasi L1/L2 periodik (tanpa redraw)
const uint32_t TFT_PERIOD_MS  = 1000;  // redraw halaman aktif
const uint32_t UI_PAGE_MS     = 8000;  // auto-rotate halaman (non-blocking)
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
uint32_t t_summary = 0, t_eval = 0, t_tft = 0, t_wifi = 0, t_http = 0;
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

// ---------------- TFT + UI state (v4) ----------------
Adafruit_ST7735 tft = Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);
const uint8_t  UI_PAGE_COUNT = 5;
uint8_t  uiPage = 0;
uint32_t lastUiPageMs = 0;
bool     btnLastNext = HIGH, btnLastBack = HIGH;
uint32_t lastBtnMs = 0, lastBuzzMs = 0, statusBeepUntil = 0, lastPulseMs = 0;
bool     buzzOn = false, pulseOn = false;
uint8_t  lastSeenLevel = 255;   // 255 = belum ada data -> transisi pertama tak bunyi
// palet panel (mengikuti receiver PALD v4.1)
const uint16_t PANEL_BG    = 0x1082;
const uint16_t DIVIDER     = 0x3186;
const uint16_t DIM         = 0x8410;
const uint16_t DARK_GREEN  = 0x0200;
const uint16_t DARK_YELLOW = 0x4200;
const uint16_t DARK_ORANGE = 0x4100;
const uint16_t DARK_RED    = 0x4000;

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
// Risiko per node dari state terakhir + hujan saat ini — dipakai L2 & UI.
// Satu pintu supaya angka di layar = angka yang dievaluasi (tanpa drift).
static inline float nodeRisk(uint8_t i) { return f1_risk(levelPctOf(i), iqOf(i), g_rain_mm3h); }
static inline uint8_t nodeStatus4(uint8_t i) { return f1_status4(nodeRisk(i)); }

void evaluateLayer(uint8_t id_new) {
  // ---- Layer 1 per node (yang baru datang + recompute semua utk konsistensi)
  uint8_t l1_of[4] = {0,0,0,0};
  uint8_t status_global = 0;
  uint8_t online_count = 0;
  for (uint8_t i = 1; i <= 3; i++) if (nodeOnline(i)) online_count++;
  if (online_count == 0) {                    // BUG 2: tanpa data bukan AMAN
    scr_status_global = 4;                    // NO_DATA
    scr_kelas = L2_CAMPURAN; scr_konf = 0;    // netral; JSON/TFT menampilkan NO_DATA
    return;
  }
  for (uint8_t i = 1; i <= 3; i++) {
    if (!nodeOnline(i)) { l1_of[i] = 0; continue; }
    const float risk = nodeRisk(i);
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
    const float risk = nodeRisk(p.node_id);
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
    const float risk = nodeRisk(p.node_id);
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

// ═════════════════ TFT UI (v4 — menggantikan drawOLED v3) ═════════════════
// Warna status: 0=AMAN 1=WASPADA 2=SIAGA 3=BAHAYA 4=NO_DATA
// Lebar font klasik: 6 px/char (size1), 12 px/char (size2) — layout dihitung
// dgn asumsi ini; x maks gambar = 160 (layar 160x128 landscape).
static inline uint16_t statusColor(uint8_t s) {
  switch (s) {
    case 0:  return ST77XX_GREEN;
    case 1:  return ST77XX_YELLOW;
    case 2:  return ST77XX_ORANGE;
    case 3:  return ST77XX_RED;
    default: return ST77XX_YELLOW;   // NO_DATA — kuning: sistem buta, bukan aman
  }
}
static inline uint16_t statusDark(uint8_t s) {
  switch (s) {
    case 0:  return DARK_GREEN;
    case 1:  return DARK_YELLOW;
    case 2:  return DARK_ORANGE;
    case 3:  return DARK_RED;
    default: return DARK_YELLOW;
  }
}
// Warna bar header/hero: cyan sebelum ada paket; kuning saat NO_DATA.
static inline uint16_t headerBarColor() {
  if (!scr_valid) return ST77XX_CYAN;
  if (scr_status_global == 4) return ST77XX_YELLOW;
  return statusColor(scr_status_global);
}

// Baris label kiri — value kanan (mulai x=93). Standar halaman SISTEM/HUJAN.
void tftKV(int y, const __FlashStringHelper *label, const char *value, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(6, y);
  tft.print(label);
  tft.setTextColor(color);
  tft.setCursor(93, y);
  tft.print(value);
  tft.setTextColor(ST77XX_WHITE);
}

// Header seragam halaman 1-4: bar warna status + counter halaman + titik WiFi.
void tftHeader(const __FlashStringHelper *title) {
  tft.fillRoundRect(0, 0, 160, 15, 4, headerBarColor());
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(4, 4);
  tft.print(title);
  if (WiFi.status() == WL_CONNECTED) tft.fillCircle(154, 7, 2, ST77XX_BLACK);
  tft.setCursor(132, 4);
  tft.print(uiPage + 1);
  tft.print('/');
  tft.print(UI_PAGE_COUNT);
  tft.setTextColor(ST77XX_WHITE);
}

void tftPageFooter() {
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(6, 119);
  tft.print(F("BACK <       NEXT >"));
  tft.setTextColor(ST77XX_WHITE);
}

// Bungkus teks per kata: maks maxChars/baris, maks maxLines baris (sisanya dipotong).
void drawWrapped(const char *s, int16_t x, int16_t y, uint16_t color,
                 uint8_t maxChars, uint8_t maxLines) {
  tft.setTextSize(1);
  tft.setTextColor(color);
  uint16_t n = strlen(s), i = 0;
  uint8_t line = 0;
  while (i < n && line < maxLines) {
    uint16_t take = n - i;
    if (take > maxChars) {
      take = maxChars;
      while (take > 0 && s[i + take] != ' ') take--;   // mundur ke spasi terakhir
      if (take == 0) take = maxChars;                  // kata tunggal kepanjangan
    }
    tft.setCursor(x, y + line * 8);
    for (uint16_t k = 0; k < take; k++) tft.write(s[i + k]);
    i += take;
    while (i < n && s[i] == ' ') i++;
    line++;
  }
  tft.setTextColor(ST77XX_WHITE);
}

// Baris-2 halaman NODE: "331mm 100% Q99.9 IQ9.9" / "level INVALID" / "--"
void nodeDetail(uint8_t i, char *out, size_t outN) {
  if (!nodes[i].seen) { strlcpy(out, "--", outN); return; }
  if (nodes[i].level_mm == LEVEL_INVALID) { strlcpy(out, "level INVALID", outN); return; }
  char qb[12], ib[12];
  if (nodes[i].q10 == 0xFFFF) { strlcpy(qb, "Q--", sizeof(qb)); strlcpy(ib, "IQ--", sizeof(ib)); }
  else {
    snprintf(qb, sizeof(qb), "Q%.1f", nodes[i].q10 / 10.0f);
    snprintf(ib, sizeof(ib), "IQ%.1f", iqOf(i));
  }
  snprintf(out, outN, "%dmm %d%% %s %s", (int)nodes[i].level_mm,
           (int)(levelPctOf(i) + 0.5f), qb, ib);
}

// ── Halaman 0: DASHBOARD ────────────────────────────────────────────────
void drawDashboard() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRoundRect(0, 0, 160, 15, 4, headerBarColor());
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(4, 4);  tft.print(F("FGV  /  GATEWAY"));
  if (WiFi.status() == WL_CONNECTED) tft.fillCircle(154, 7, 2, ST77XX_BLACK);
  tft.setCursor(132, 4); tft.print(uiPage + 1); tft.print('/'); tft.print(UI_PAGE_COUNT);

  // Hero: status kawasan + pola (warna panel ikut status; NO_DATA = kuning).
  const uint8_t st = scr_status_global;
  uint16_t bright = ST77XX_CYAN, dark = 0x0821;
  if (scr_valid) { bright = statusColor(st); dark = statusDark(st); }
  tft.fillRoundRect(4, 19, 152, 45, 5, dark);
  tft.drawFastHLine(10, 22, 100, DIVIDER);
  tft.setTextColor(0xC618);
  tft.setCursor(10, 25); tft.print(F("STATUS KAWASAN"));
  tft.setTextColor(bright);
  tft.setTextSize(2);
  tft.setCursor(10, 34);
  if (!scr_valid) tft.print(F("MENUNGGU"));
  else            tft.print(gwStatusName(st));   // AMAN..BAHAYA / NO_DATA
  tft.setTextSize(1);
  tft.setCursor(10, 52);
  if (!scr_valid)   { tft.setTextColor(ST77XX_WHITE);  tft.print(F("Menunggu node 1/2/3...")); }
  else if (st == 4) { tft.setTextColor(ST77XX_YELLOW); tft.print(F("Semua node offline")); }
  else {
    tft.setTextColor(ST77XX_WHITE); tft.print(F("POLA "));
    tft.setTextColor(ST77XX_CYAN);  tft.print(L2_KELAS_NAME[scr_kelas]);
  }

  // Chip 3 node: level mm; warna = status node; '!' jika >=75% kapasitas kolam.
  for (uint8_t i = 1; i <= 3; i++) {
    const int16_t x = 4 + (i - 1) * 52;
    tft.fillRoundRect(x, 67, 48, 18, 4, PANEL_BG);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(x + 4, 73);
    tft.print(F("N")); tft.print(i);
    if (!nodeOnline(i))                          { tft.setTextColor(DIM); tft.print(F(" --")); }
    else if (nodes[i].level_mm == LEVEL_INVALID) { tft.setTextColor(DIM); tft.print(F(" ??")); }
    else {
      tft.setTextColor(statusColor(nodeStatus4(i)));
      tft.print(F(" ")); tft.print(nodes[i].level_mm);
      if (levelPctOf(i) >= 75.0f) tft.print(F("!"));
    }
  }

  // Panel hujan (input fuzzy L1) + panel radio.
  tft.fillRoundRect(4, 88, 73, 31, 4, PANEL_BG);
  tft.fillRoundRect(82, 88, 74, 31, 4, PANEL_BG);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 91);  tft.print(F("HUJAN"));
  tft.setCursor(87, 91); tft.print(F("RADIO"));
  // tag sumber hujan (kanan atas panel A)
  tft.setCursor(45, 91);
  if (g_sim_aktif)                  { tft.setTextColor(ST77XX_RED);    tft.print(F("SIM")); }
  else if (g_bmkg_status[0] == 'A') { tft.setTextColor(ST77XX_WHITE);  tft.print(F("BMKG")); }
  else if (g_bmkg_status[0] == 'S') { tft.setTextColor(ST77XX_YELLOW); tft.print(F("STALE")); }
  else                              { tft.setTextColor(ST77XX_RED);    tft.print(F("OFF")); }
  // nilai hujan (input fuzzy) — desimal dipangkas utk nilai >=100
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 100);
  tft.print(g_rain_mm3h < 100.0f ? String(g_rain_mm3h, 1) : String(g_rain_mm3h, 0));
  tft.print(F(" mm/3jam"));
  // info baris-3 panel A
  tft.setTextColor(DIM);
  tft.setCursor(8, 110);
  if (g_sim_aktif)                  tft.print(F("SIM AKTIF"));
  else if (g_bmkg_last_ok == 0)     tft.print(F("NO DATA"));
  else if (g_bmkg_status[0] == 'S') tft.print(F("DATA USANG"));
  else { tft.print(F("upd ")); tft.print((millis() - g_bmkg_last_ok) / 60000UL); tft.print(F(" mnt")); }
  // panel B: RX + RSSI
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(87, 100); tft.print(F("RX ")); tft.print(n_ok % 1000000UL);
  tft.setCursor(87, 110); tft.print(F("RSSI ")); tft.print((int)scr_rssi);

  // Footer: PDR (kiri) + lost (kanan)
  tft.setTextColor(DIM);
  tft.setCursor(6, 119);
  const uint32_t tot = n_ok + n_crc_bad + n_lost + n_foreign;
  if (tot) { tft.print(F("PDR ")); tft.print(n_ok * 100.0f / tot, 1); tft.print('%'); }
  else       tft.print(F("PDR --"));
  tft.setCursor(94, 119);
  tft.print(F("lost ")); tft.print(n_lost % 100000UL);
  tft.setTextColor(ST77XX_WHITE);
}

// ── Halaman 1: NODE (detail per node hulu/tengah/hilir) ─────────────────
void drawPageNode() {
  tft.fillScreen(ST77XX_BLACK);
  tftHeader(F("NODE"));
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(7, 20); tft.print(F("HULU / TENGAH / HILIR"));

  static const char *NAMES[4] = { "", "HULU", "TENGAH", "HILIR" };
  for (uint8_t i = 1; i <= 3; i++) {
    const int16_t y = 30 + (int16_t)(i - 1) * 23;
    const bool on = nodeOnline(i);
    tft.fillRoundRect(4, y, 152, 21, 4, PANEL_BG);
    tft.fillCircle(12, y + 10, 3, on ? statusColor(nodeStatus4(i)) : ST77XX_RED);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, y + 3); tft.print(NAMES[i]);
    tft.setTextColor(on ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(58, y + 3); tft.print(on ? F("ONLINE") : F("OFFLINE"));
    // tag kanan baris-1: MELUAP (flags bit1) atau GPS fix (bit2)
    if (on && (nodes[i].flags & 0x02))      { tft.setTextColor(ST77XX_RED);   tft.setCursor(104, y + 3); tft.print(F("MELUAP")); }
    else if (on && (nodes[i].flags & 0x04)) { tft.setTextColor(ST77XX_GREEN); tft.setCursor(104, y + 3); tft.print(F("GPS")); }
    // baris-2: level/Q/IQ (nilai terakhir walaupun node offline)
    char det[40];
    nodeDetail(i, det, sizeof(det));
    tft.setTextColor(on ? ST77XX_WHITE : DIM);
    tft.setCursor(20, y + 11); tft.print(det);
  }

  tft.fillRoundRect(4, 104, 152, 9, 3, PANEL_BG);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 104); tft.print(F("RSSI/SNR"));
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(93, 104); tft.print((int)scr_rssi); tft.print('/'); tft.print(scr_snr, 1);
  tftPageFooter();
}

// ── Halaman 2: POLA L2 (klasifikasi spasial + konfirmasi backwater) ─────
void drawPagePola() {
  tft.fillScreen(ST77XX_BLACK);
  tftHeader(F("POLA L2"));
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(7, 18); tft.print(F("KLASIFIKASI POLA SPASIAL"));

  const bool no_data = (scr_status_global == 4);
  tft.setTextSize(2);
  tft.setCursor(8, 27);
  if (!scr_valid)   { tft.setTextColor(ST77XX_CYAN);   tft.print(F("MENUNGGU")); }
  else if (no_data) { tft.setTextColor(ST77XX_YELLOW); tft.print(F("NO_DATA")); }
  else              { tft.setTextColor(ST77XX_WHITE);  tft.print(L2_KELAS_NAME[scr_kelas]); }
  tft.setTextSize(1);
  // deskripsi kelas (maks 2 baris)
  if (!scr_valid)   drawWrapped("Menunggu paket node...", 8, 47, DIM, 24, 1);
  else if (no_data) drawWrapped("Belum ada node online", 8, 47, DIM, 24, 1);
  else              drawWrapped(L2_KELAS_DESC[scr_kelas], 8, 47, DIM, 24, 2);

  tft.drawFastHLine(8, 64, 144, DIVIDER);
  // KONFIRMASI backwater (kolom value x=70 supaya "SUMBATAN KUAT" muat)
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(6, 68); tft.print(F("KONFIRMASI"));
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(70, 68);
  if      (!scr_valid || scr_kelas != L2_BACKWATER) tft.print(F("--"));
  else if (scr_konf == 1) tft.print(F("SUMBATAN KUAT"));
  else if (scr_konf == 2) tft.print(F("STAGNAN"));
  else if (scr_konf == 3) tft.print(F("DEBIT NAIK"));
  else                    tft.print(F("tanpa info IQ"));
  // LAG hulu -> hilir (klok gateway)
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(6, 79); tft.print(F("LAG H->D"));
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(70, 79);
  if (lag_valid) { tft.print(lag_hulu_hilir_ms); tft.print(F(" ms")); }
  else tft.print(F("--"));
  // IQ HILIR (rasio Q/Qref — dasar konfirmasi backwater)
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(6, 90); tft.print(F("IQ HILIR"));
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(70, 90);
  if (nodeOnline(3) && nodes[3].q10 != 0xFFFF) tft.print(iqOf(3), 3);
  else tft.print(F("--"));
  // AKSI (maks 2 baris, ikut aksi dashboard JSON)
  char abuf[72];
  if (!scr_valid)   strlcpy(abuf, "AKSI: menunggu data...", sizeof(abuf));
  else if (no_data) strlcpy(abuf, "AKSI: Periksa daya/radio node", sizeof(abuf));
  else              snprintf(abuf, sizeof(abuf), "AKSI: %s", L2_AKSI[scr_kelas]);
  drawWrapped(abuf, 6, 100, ST77XX_YELLOW, 25, 2);
  tftPageFooter();
}

// ── Halaman 3: HUJAN (sumber hujan utk fuzzy L1) ────────────────────────
void drawPageHujan() {
  tft.fillScreen(ST77XX_BLACK);
  tftHeader(F("HUJAN"));
  tft.setTextSize(1);
  tft.setCursor(7, 19);
  if (g_sim_aktif) { tft.setTextColor(ST77XX_YELLOW); tft.print(F("SUMBER: SIMULASI DEMO")); }
  else             { tft.setTextColor(DIM);           tft.print(F("SUMBER: PRAKIRAAN BMKG")); }

  tft.fillRoundRect(5, 25, 73, 50, 4, PANEL_BG);
  tft.fillRoundRect(82, 25, 73, 50, 4, PANEL_BG);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 28); tft.print(F("INPUT FUZZY"));
  tft.setCursor(87, 28); tft.print(F("PRAKIRAAN"));
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 38);
  tft.print(g_rain_mm3h < 100.0f ? String(g_rain_mm3h, 1) : String(g_rain_mm3h, 0));
  tft.setCursor(87, 38);
  if (g_bmkg_last_ok != 0) tft.print(g_rain_tp_mm3h < 100.0f ? String(g_rain_tp_mm3h, 1) : String(g_rain_tp_mm3h, 0));
  else tft.print(F("--"));
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(10, 58); tft.print(F("mm/3jam"));
  tft.setCursor(87, 58); tft.print(F("mm/3jam"));
  tft.setCursor(10, 66);
  if (g_sim_aktif)                  { tft.setTextColor(ST77XX_RED);    tft.print(F("SIM")); }
  else if (g_bmkg_status[0] == 'A') { tft.setTextColor(ST77XX_GREEN);  tft.print(F("BMKG")); }
  else if (g_bmkg_status[0] == 'S') { tft.setTextColor(ST77XX_YELLOW); tft.print(F("STALE")); }
  else                              { tft.setTextColor(ST77XX_RED);    tft.print(F("OFF")); }
  tft.setCursor(87, 66);
  if (g_bmkg_last_ok != 0) {
    tft.print(F("upd ")); tft.print((millis() - g_bmkg_last_ok) / 60000UL); tft.print(F(" mnt"));
  } else tft.print(F("belum ada"));

  // Baris info: status + update + override + catatan kejujuran
  const char *bmkgState = (g_bmkg_status[0] == 'A') ? "AKTIF" :
                          (g_bmkg_status[0] == 'S') ? "STALE" : "OFFLINE";
  uint16_t bmkgCol = (g_bmkg_status[0] == 'A') ? ST77XX_GREEN :
                     (g_bmkg_status[0] == 'S') ? ST77XX_YELLOW : ST77XX_RED;
  tftKV(78, F("STATUS BMKG"), bmkgState, bmkgCol);

  char ageb[16];
  if (g_bmkg_last_ok != 0) snprintf(ageb, sizeof(ageb), "%lu mnt", (unsigned long)((millis() - g_bmkg_last_ok) / 60000UL));
  else strlcpy(ageb, "belum ada", sizeof(ageb));
  tftKV(89, F("UPDATE"), ageb, ST77XX_WHITE);

  char simb[16];
  if (g_sim_aktif) snprintf(simb, sizeof(simb), "AKTIF %.1f", g_sim_rain_mm3h);
  else strlcpy(simb, "nonaktif", sizeof(simb));
  tftKV(100, F("SIM OVERRIDE"), simb, g_sim_aktif ? ST77XX_YELLOW : DIM);

  tft.setTextColor(DIM);
  tft.setCursor(11, 111); tft.print(F("Tanpa sensor hujan fisik"));
  tftPageFooter();
}

// ── Halaman 4: SISTEM ───────────────────────────────────────────────────
void drawPageSystem() {
  tft.fillScreen(ST77XX_BLACK);
  tftHeader(F("SISTEM"));
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  // counter radio ringkas (mirror SUMMARY serial)
  tft.setCursor(6, 18);
  tft.print(F("ok")); tft.print(n_ok % 10000UL);
  tft.print(F(" crc")); tft.print(n_crc_bad % 1000UL);
  tft.print(F(" lost")); tft.print(n_lost % 10000UL);

  tftKV(27, F("DISPLAY"), "ST7735", ST77XX_GREEN);
  tftKV(41, F("LORA"), scr_valid ? "RX AKTIF" : "WAIT NODE", scr_valid ? ST77XX_GREEN : ST77XX_YELLOW);
  tftKV(55, F("WIFI"), (WiFi.status() == WL_CONNECTED) ? "TERHUBUNG" : "OFFLINE",
        (WiFi.status() == WL_CONNECTED) ? ST77XX_GREEN : ST77XX_RED);
  // IP
  tft.setTextColor(ST77XX_CYAN); tft.setCursor(6, 69); tft.print(F("IP"));
  tft.setTextColor(ST77XX_WHITE); tft.setCursor(30, 69); tft.print(WiFi.localIP());
  // Firebase push ok/fail
  char fb[16];
  snprintf(fb, sizeof(fb), "%lu/%lu", (unsigned long)http_ok, (unsigned long)http_fail);
  tftKV(83, F("FIREBASE"), fb, (http_fail == 0) ? ST77XX_GREEN : ST77XX_YELLOW);
  // Uptime
  const uint32_t up = millis() / 1000UL;
  char ub[20];
  snprintf(ub, sizeof(ub), "%02lu:%02lu:%02lu",
           (unsigned long)(up / 3600UL), (unsigned long)((up / 60UL) % 60UL), (unsigned long)(up % 60UL));
  tftKV(97, F("UPTIME"), ub, ST77XX_WHITE);
  // kredit + versi
  tft.setTextColor(DIM);
  tft.setCursor(6, 111); tft.print(F("FGV TEAM  FW v4.0"));
  tftPageFooter();
}

void drawUiPage() {
  switch (uiPage) {
    case 0:  drawDashboard();  break;
    case 1:  drawPageNode();   break;
    case 2:  drawPagePola();   break;
    case 3:  drawPageHujan();  break;
    default: drawPageSystem(); break;
  }
}

// ── Tombol + buzzer (pola dari receiver PALD v4.1, semua non-blocking) ──
void statusChirp(uint16_t ms) { statusBeepUntil = millis() + ms; }

void handleButtons() {
  uint32_t now = millis();
  bool n = digitalRead(BTN_NEXT);
  bool b = digitalRead(BTN_BACK);
  if (now - lastBtnMs > 180) {   // debounce sederhana
    if (n == LOW && btnLastNext == HIGH) {
      lastBtnMs = now;
      uiPage = (uiPage + 1) % UI_PAGE_COUNT;
      drawUiPage();
      lastUiPageMs = millis();
      statusChirp(30);           // klik halus sebagai feedback tombol
    } else if (b == LOW && btnLastBack == HIGH) {
      lastBtnMs = now;
      uiPage = (uiPage + UI_PAGE_COUNT - 1) % UI_PAGE_COUNT;
      drawUiPage();
      lastUiPageMs = millis();
      statusChirp(30);
    }
  }
  btnLastNext = n;
  btnLastBack = b;
}

// Buzzer: alarm BAHAYA (250 ms bebek) + chirp status, semua via millis.
void buzzTick() {
  uint32_t now = millis();
  // Kanal 1 (ALARM): bebek selama status global BAHAYA
  const bool bahaya = scr_valid && scr_status_global == 3;
  if (bahaya) {
    if (now - lastBuzzMs >= 250) {
      lastBuzzMs = now;
      buzzOn = !buzzOn;
      digitalWrite(BUZZER_ALARM, buzzOn ? HIGH : LOW);
    }
  } else if (buzzOn) {
    buzzOn = false;
    digitalWrite(BUZZER_ALARM, LOW);
  }
  // Kanal 2 (STATUS): chirp 90 ms saat status global berubah (arah mana pun)
  digitalWrite(BUZZER_STATUS, (now < statusBeepUntil) ? HIGH : LOW);
  if (scr_valid && lastSeenLevel != 255 && scr_status_global != lastSeenLevel) statusChirp(90);
  if (scr_valid) lastSeenLevel = scr_status_global;
}

// Bingkai merah berkedip saat BAHAYA — sinyal visual darurat.
void alertPulseTick() {
  if (scr_valid && scr_status_global == 3) {
    if (millis() - lastPulseMs >= 600) {
      lastPulseMs = millis();
      pulseOn = !pulseOn;
      tft.drawRect(0, 0, 159, 127, pulseOn ? ST77XX_RED : ST77XX_BLACK);
    }
  } else if (pulseOn) {
    pulseOn = false;
    tft.drawRect(0, 0, 159, 127, ST77XX_BLACK);
  }
}

// ── Boot screen: checklist + progress bar (pola receiver PALD) ──────────
void bootBar(uint8_t pct) {
  if (pct > 100) pct = 100;
  tft.drawRect(20, 104, 120, 10, ST77XX_WHITE);
  tft.fillRect(22, 106, (int16_t)(116 * pct / 100), 6, ST77XX_GREEN);
}
void bootRow(uint8_t idx, const char *label) {
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 54 + idx * 12);
  tft.print(label);
  for (uint8_t i = strlen(label); i < 12; i++) tft.print(F("."));
}
void bootStatus(uint8_t idx, bool ok) {
  tft.setCursor(134, 54 + idx * 12);
  if (ok) { tft.setTextColor(ST77XX_GREEN); tft.print(F("OK")); }
  else    { tft.setTextColor(ST77XX_RED);   tft.print(F("FAIL")); }
  tft.setTextColor(ST77XX_WHITE);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("FGV GATEWAY v4 | Receiver TFT (basis PALD) | 3-node + Fuzzy L1 + Pattern L2"));

  esp_task_wdt_config_t cfg = { .timeout_ms = (uint32_t)WDT_S * 1000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_deinit();
  esp_task_wdt_init(&cfg);
  esp_task_wdt_add(NULL);
  Serial.println(F("Watchdog aktif"));

  // SPI bus dipakai bersama LoRa + TFT (chip-select beda; kedua lib pakai SPI transaction)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // TFT + peripheral HMI (wiring receiver PALD)
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);            // backlight penuh
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BUZZER_ALARM, OUTPUT);  digitalWrite(BUZZER_ALARM, LOW);
  pinMode(BUZZER_STATUS, OUTPUT); digitalWrite(BUZZER_STATUS, LOW);
  tft.initR(INITR_BLACKTAB);              // ST7735S; kalau warna kebalik -> tft.invertDisplay(true)
  tft.setRotation(1);                     // landscape 160x128
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 20);
  tft.print(F("FGV GATEWAY"));
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(10, 42);
  tft.print(F("Receiver LoRa - 3 node"));
  bootBar(0);
  bootRow(0, "LoRa 433MHz");

  // LoRa — radio config FGV TIDAK berubah dari v3 (sync 0xF1 sengaja beda dari PALD)
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(RF_FREQ_HZ)) {
    Serial.println(F("FATAL: LoRa.begin gagal."));
    tft.fillScreen(ST77XX_RED);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 56);
    tft.print(F("LoRa FAIL"));
    while (true) { esp_task_wdt_reset(); delay(1000); }
  }
  LoRa.setSpreadingFactor(RF_SF);
  LoRa.setSignalBandwidth(RF_BW);
  LoRa.setCodingRate4(RF_CR);
  LoRa.setSyncWord(RF_SYNCWORD);
  LoRa.enableCrc();
  LoRa.onReceive(onReceive);
  LoRa.receive();
  Serial.println(F("LoRa OK 433 MHz | SF9 BW125 CR5 sync 0xF1 CRC on"));
  Serial.println(F("TFT: CS=6 DC=7 RST=4 BLK=5 (SCK/MOSI share LoRa)"));
  bootStatus(0, true);
  bootBar(30);
  bootRow(1, "WiFi");

  // WiFi: percobaan saat boot (utk checklist); wifiMaintain() jaga koneksi di loop
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t t0 = millis();
  uint8_t bootTick = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(400);
    esp_task_wdt_reset();                  // wait-loop bisa > timeout WDT (8 dtk)
    Serial.print(F("."));
    bootTick = (bootTick + 1) % 20;
    bootBar(35 + bootTick);
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifi_ok = true;
    Serial.print(F("\n[WIFI] terhubung, IP: "));
    Serial.println(WiFi.localIP());
    bootStatus(1, true);
    bootBar(60);
    bootRow(2, "BMKG 3jam");
    esp_task_wdt_reset();
    fetchBMKG();                           // sekali saat boot; retry di loop kalau gagal
    bootStatus(2, g_bmkg_last_ok != 0);
    bootBar(85);
    bootRow(3, "Firebase");
    esp_task_wdt_reset();
    postToDashboard();                     // feedback dini dashboard (best effort)
    esp_task_wdt_reset();
    bootStatus(3, http_ok > 0);
    bootBar(100);
    t_bmkg = (g_bmkg_last_ok != 0) ? millis() : 0;   // sukses -> fetch lagi 15 mnt
  } else {
    Serial.println(F("\nWARN: WiFi gagal - mode LoRa-only, retry di loop"));
    bootStatus(1, false);
    bootRow(2, "BMKG 3jam");  bootStatus(2, false);
    bootRow(3, "Firebase");   bootStatus(3, false);
    bootBar(100);
  }

  Serial.println(F("Menunggu paket node 1/2/3..."));
  t_summary = millis(); t_eval = millis(); t_tft = millis(); t_wifi = millis(); t_http = millis();
  t_sim = 0;                               // poll simulasi segera (kalau WiFi hidup)
  lastUiPageMs = millis();

  // Transisi wipe ke layar utama
  for (int16_t x = 0; x < 160; x += 10) {
    tft.fillRect(x, 0, 10, 128, ST77XX_BLACK);
    delay(15);
  }
  drawUiPage();
  Serial.println(F("UI siap: 5 halaman auto-rotate 8 dtk | NEXT=1 BACK=2"));
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
  // Tombol + buzzer dulu — responsif walau LoRa menunggu paket
  handleButtons();
  buzzTick();

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

  // Re-evaluasi periodik (BUG 3): offline -> NO_DATA, parsial -> CAMPURAN
  if (millis() - t_eval >= EVAL_PERIOD_MS) { t_eval = millis(); evaluateLayer(0); }

  // Redraw halaman aktif + kedip darurat (non-blocking)
  if (millis() - t_tft >= TFT_PERIOD_MS) { t_tft = millis(); drawUiPage(); }
  alertPulseTick();

  // Rotasi halaman otomatis; timer di-reset saat tombol dipakai
  if (millis() - lastUiPageMs >= UI_PAGE_MS) {
    uiPage = (uiPage + 1) % UI_PAGE_COUNT;
    lastUiPageMs = millis();
    drawUiPage();
  }

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
