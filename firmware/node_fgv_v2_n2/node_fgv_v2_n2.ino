/*
 * FGV NODE v2 — JSN + MANNING on-chip (+ LoRa TX)
 * ================================================
 * Upgrade poc3_node: setelah level valid, hitung v & Q (Manning) on-chip
 * via flow_manning.h, dan kirim Q di paket. Pola sensor/radio tidak
 * berubah dari PoC yang sudah LULUS (median/5, INVALID=0xFFFF, CRC8).
 *
 * PAKET (18 byte, packed):
 *   node_id | seq | level_mm u16 | q_lpm_x10 u16 | v_cms u16 |
 *   flags u8 | crc8 | lat_e7 i32 | lon_e7 i32   (GPS pin-point lokasi)
 *   level_mm = 0xFFFF -> INVALID (Q juga 0xFFFF)
 *   lat/lon  = 0x7FFFFFFF -> GPS belum fix (INVALID — bukan 0 palsu)
 *   v dalam cm/s (x1), Q dalam L/min x10
 *
 * GEOMETRI (terukur 20 Sep 2026 — lihat flow_manning.h):
 *   b=8cm, tinggi=2.5cm, L=45cm, S=4/450=0.00889, n terkalibrasi 0.0154
 * SENSOR: pasang di bracket, jarak sensor->dasar >= 280 mm!
 *   (atur: level_mm = SENSOR_HEIGHT_MM - jarak_terbaca)
 *
 * Board: esp32:esp32:esp32s3 · Serial: 115200
 * ⚠️ ANTENA TERPASANG SEBELUM JALAN.
 */

#include <LoRa.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <stdint.h>
#include <TinyGPSPlus.h>
#include "flow_manning.h"

// Watchdog: loop macet > 8 dtk -> panic-reboot otomatis (anti "sunyi misterius")
#define WDT_TIMEOUT_S 8

// ---------------- Pin map (ARSITEKTUR §2.1) -------------------------------
#define LORA_NSS   10
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_RST   9
#define LORA_DIO0  8

const uint8_t PIN_TRIG = 5;
const uint8_t PIN_ECHO = 4;

// ---------------- GPS Neo-6M (pin point lokasi per node) -------------------
// Arsitektur 3-box independen di titik berbeda: tiap node melaporkan posisinya
// sendiri. Wiring mengikuti pola PALD test_gps_neo6m (terbukti): UART2, 9600.
// Cukup 3 kabel: VCC -> 3V3, GND -> GND, GPS TX -> GPIO 16.
// (GPS RX ke GPIO 17 opsional — kita tidak mengirim config ke modul.)
#define GPS_RX_PIN 16   // S3 RX  <- GPS TX
#define GPS_TX_PIN 17   // S3 TX  -> GPS RX (boleh dibiarkan tak tersambung)
#define GPS_BAUD    9600
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
bool    gps_fix = false;
int32_t gps_lat_e7 = 0x7FFFFFFF;   // sentinel = INVALID (belum fix)
int32_t gps_lon_e7 = 0x7FFFFFFF;

// ---------------- Sensor (keputusan PoC-1, tervalidasi) --------------------
const float SPEED_OF_SOUND_MPS = 343.42f;
const float US_TO_MM           = SPEED_OF_SOUND_MPS / 2.0f / 1000.0f;
const uint8_t  N_SAMPLES         = 5;
const uint32_t PING_INTERVAL_MS  = 60;
const uint32_t ECHO_TIMEOUT_US   = 30000;
const float    MM_MIN            = 200.0f;
const float    MM_MAX            = 4500.0f;
const uint8_t  INVALID_TOLERANCE = 3;
const float    LEVEL_OFFSET_MM   = -3.6f;   // kalibrasi PoC-1 (+3.6 terukur)
const uint16_t LEVEL_INVALID     = 0xFFFF;

// *** TERUKUR 20/21 Sep (well diganti panjang): kering 331 mm, banjir 240 mm
//     -> level banjir = 91 mm. Bacaan 240 mm < blind zone datasheet, tapi
//     unit ini tervalidasi empiris stabil hingga 221 mm (HASIL PoC-1/3).
float SENSOR_HEIGHT_MM = 331.0f;

// Kapasitas wadah bening (Kolam Penenang Hilir): kering 331 mm, banjir 240 mm
// Level > 85 mm (saat kran pink ditutup/dihambat) = MELUAP (BANJIR)
// Q disaturasi pada Q maks saluran.
const float LEVEL_OVERFLOW_MM = 85.0f;

// ---------------- Radio (tervalidasi PoC-2/3) ------------------------------
const long    RF_FREQ_HZ   = 433000000L;
const int     RF_SF        = 9;
const long    RF_BW        = 125E3;
const int     RF_CR        = 5;
const uint8_t RF_SYNCWORD = 0xF1;
const uint32_t TX_PERIOD_MS = 3000;   // 3 node 1 channel: 3 s + fase TDMA per node

const uint8_t NODE_ID = 2;

struct __attribute__((packed)) FgvTx {
  uint8_t  node_id;
  uint8_t  seq;
  uint16_t level_mm;     // 0xFFFF = INVALID (level dari DASAR saluran)
  uint16_t q_lpm_x10;    // debit x10 L/min (0xFFFF saat INVALID; saturasi saat MELUAP)
  uint16_t v_cms;        // kecepatan cm/s
  uint8_t  flags;        // bit0=level_ok bit1=meluap bit2=gps_fix
  uint8_t  crc8;
  int32_t  lat_e7;       // derajat x 1e7; 0x7FFFFFFF = belum fix
  int32_t  lon_e7;
};                         // total 18 byte
static_assert(sizeof(FgvTx) == 18, "FGV node packet must stay exactly 18 bytes");

uint8_t crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

uint8_t packetCrc8(const uint8_t *packet) {
  // CRC byte is at offset 9 in packed FgvTx; omit that byte from the CRC input.
  uint8_t crc = 0;
  for (uint8_t i = 0; i < sizeof(FgvTx); i++) {
    if (i == 9) continue;
    crc ^= packet[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

// ---------------- Sensor ----------------------------------------------------
float takePingMm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  const uint32_t echo_us = pulseIn(PIN_ECHO, HIGH, 30000);
  if (echo_us == 0) return NAN;
  const float mm = (float)echo_us * US_TO_MM;
  if (mm < MM_MIN || mm > MM_MAX) return NAN;
  return mm;
}

float medianOf(float *v, uint8_t n) {
  for (uint8_t i = 1; i < n; i++) {
    const float key = v[i];
    int8_t j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

// ---------------- State -----------------------------------------------------
uint8_t seq = 0;
uint32_t tx_ok = 0, tx_fail = 0, n_invalid_cycles = 0, tx_busy_skip = 0;
uint32_t tx_start_ms = 0;

// TX async: loop TIDAK PERNAH blocking menunggu TX_DONE (lihat catatan
// bug hang: endPacket sinkron = while tanpa timeout di LoRa.cpp:184-200;
// EMI pompa bisa bikin radio gagal set TX_DONE -> hang permanen).
volatile bool tx_done_flag = false;
void onTxDone() { tx_done_flag = true; }

// Pemulihan mandiri radio setelah glitch EMI (pompa on/off)
void radioRecover() {
  Serial.println(F("[RADIO] glitch terdeteksi -> pemulihan"));
  LoRa.sleep();
  delay(10);
  LoRa.idle();
  LoRa.setSpreadingFactor(RF_SF);
  LoRa.setSignalBandwidth(RF_BW);
  LoRa.setCodingRate4(RF_CR);
  LoRa.setSyncWord(RF_SYNCWORD);
  LoRa.enableCrc();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  delay(500);
  Serial.println();
  Serial.println(F("FGV NODE 2 v2.1 | JSN + Manning + GPS | paket 18B CRC8"));

  // Watchdog tugas loop: macet > 8 dtk -> reboot (dengan jejak di serial)
  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_deinit();          // bersihkan init bawaan core (abaikan err)
  esp_task_wdt_init(&wdt_cfg);
  esp_task_wdt_add(NULL);         // pantau loopTask
  Serial.println(F("Watchdog aktif (8 s) - hang akan self-recovery"));
  Serial.print(F("Geometri: b=80mm h=25mm S=0.00889 n=0.0154 | sensor_h="));
  Serial.print(SENSOR_HEIGHT_MM, 0);
  Serial.println(F(" mm (SESUAIKAN!)"));

  gpsSerial.setRxBufferSize(512);   // fase sensing JSN blocking ~400ms; buffer
                                    // 256 bawaan bisa luap oleh NMEA 9600 baud
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println(F("GPS UART2 9600 mulai. Fix butuh langit terbuka; indoor bisa lambat."));

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(RF_FREQ_HZ)) {
    Serial.println(F("FATAL: LoRa.begin gagal."));
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(RF_SF);
  LoRa.setSignalBandwidth(RF_BW);
  LoRa.setCodingRate4(RF_CR);
  LoRa.setSyncWord(RF_SYNCWORD);
  LoRa.enableCrc();
  LoRa.onTxDone(onTxDone);
  tx_done_flag = true;   // radio idle, TX pertama boleh jalan
  Serial.println(F("LoRa OK. TX async 1 paket/3 detik (fase TDMA per node)."));
  // TDMA fase bebas: geser mulai TX per node agar slot 3 s tidak bertumpuk
  // (n1 mulai 0 ms, n2 +1 s, n3 +2 s). Drift harian diabaikan utk skala demo.
  delay((uint32_t)(NODE_ID - 1) * 1000UL);
}

void loop() {
  const uint32_t t0 = millis();

  // GPS non-blocking: konsumsi NMEA yang ada di buffer UART2 (pola PALD)
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
  if (gps.location.isValid() && gps.location.isUpdated()) {
    gps_fix = true;
    gps_lat_e7 = (int32_t)(gps.location.lat() * 1e7);
    gps_lon_e7 = (int32_t)(gps.location.lng() * 1e7);
  }

  float valid[N_SAMPLES];
  uint8_t n_valid = 0, n_invalid = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    const float mm = takePingMm();
    if (isnan(mm)) n_invalid++; else valid[n_valid++] = mm;
    if (i < N_SAMPLES - 1) {
      const int32_t r = (int32_t)((i + 1) * 60) - (int32_t)(millis() - t0);
      if (r > 0) delay((uint32_t)r);
    }
  }

  uint16_t level_tx, q_tx, v_tx;
  uint8_t  flags_tx = 0;
  float level_mm = NAN;
  if (n_invalid >= INVALID_TOLERANCE ||
      n_valid < (N_SAMPLES - INVALID_TOLERANCE + 1)) {
    level_tx = 0xFFFF; q_tx = 0xFFFF; v_tx = 0xFFFF;
    n_invalid_cycles++;
  } else {
    const float dist = medianOf(valid, n_valid) + LEVEL_OFFSET_MM;
    level_mm = SENSOR_HEIGHT_MM - dist;   // level dari DASAR saluran
    if (level_mm < 0) level_mm = 0;       // sensor di atas dinding = kering
    level_tx = (uint16_t)(level_mm + 0.5f);
    flags_tx = 0x01;                      // bit0: level_ok

    float h_eff = level_mm;
    if (h_eff > LEVEL_OVERFLOW_MM) {      // MELUAP: Q saturasi, jangan hitung
      h_eff = LEVEL_OVERFLOW_MM;          //   geometri di luar kapasitas saluran
      flags_tx |= 0x02;                   // bit1: meluap
    }
    q_tx = manningQx10(h_eff);
    float v, Q;
    manningFromLevelMm(h_eff, &v, &Q);
    v_tx = (uint16_t)(v * 100.0f + 0.5f); // cm/s
  }

  if (gps_fix) flags_tx |= 0x04;         // bit2: GPS fix valid

  FgvTx p;
  p.node_id  = NODE_ID;
  p.seq      = seq;
  p.level_mm = level_tx;
  p.q_lpm_x10 = q_tx;
  p.v_cms    = v_tx;
  p.flags    = flags_tx;
  p.lat_e7   = gps_lat_e7;               // 0x7FFFFFFF bila belum fix
  p.lon_e7   = gps_lon_e7;
  p.crc8     = packetCrc8((uint8_t *)&p);

  // ---- TX async: TIDAK PERNAH blocking (anti-hang, tahan EMI pompa) ----
  bool tx_sent = false;
  if (!tx_done_flag) {
    if (millis() - tx_start_ms > 500) {   // TX sebelumnya tak selesai dlm 500ms
      radioRecover();                     //   -> pulihkan radio, jangan pernah hang
      tx_done_flag = true;
      tx_fail++;
    }
    // else: TX masih berjalan (normal ~150ms) -> siklus ini tanpa TX baru
  } else {
    tx_done_flag = false;
    tx_start_ms = millis();
    if (LoRa.beginPacket() == 1) {
      LoRa.write((uint8_t *)&p, sizeof(p));
      LoRa.endPacket(true);               // async: balik seketika; selesai via ISR
      tx_ok++;
      tx_sent = true;
    } else {
      tx_fail++;                          // radio sibuk/stuck
    }
  }

  Serial.print(F("["));
  Serial.print(NODE_ID);
  Serial.print(F("] seq="));
  Serial.print(seq);
  if (isnan(level_mm)) {
    Serial.print(F("  level=INVALID"));
  } else {
    Serial.print(F("  level="));
    Serial.print(level_tx);
    Serial.print(F("mm  Q="));
    Serial.print(q_tx / 10.0f, 1);
    Serial.print(F("L/min  v="));
    Serial.print(v_tx);
    Serial.print(F("cm/s"));
    if (flags_tx & 0x02) Serial.print(F("  ** MELUAP **"));
  }
  Serial.print(F("  TX="));
  Serial.print(tx_sent ? F("OK") : F("SKIP/FAIL"));
  Serial.print(F("  ok="));
  Serial.print(tx_ok);
  Serial.print(F(" fail="));
  Serial.println(tx_fail);
  if (seq % 10 == 0) {
    Serial.print(F("[GPS] "));
    if (gps_fix) {
      Serial.print(gps_lat_e7 / 1e7, 6);
      Serial.print(F(", "));
      Serial.print(gps_lon_e7 / 1e7, 6);
      Serial.print(F("  satelit="));
      Serial.println(gps.satellites.value());
    } else {
      Serial.print(F("belum fix - "));
      Serial.print(gps.charsProcessed());
      Serial.println(F(" NMEA diterima. Indoor: dekati jendela / ke luar."));
    }
  }
  seq++;

  const int32_t remain = (int32_t)TX_PERIOD_MS - (int32_t)(millis() - t0);
  if (remain > 0) delay((uint32_t)remain);
  esp_task_wdt_reset();   // loop hidup & selesai satu siklus -> laporkan ke WDT
}
