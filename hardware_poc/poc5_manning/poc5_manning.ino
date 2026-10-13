/*
 * FGV — Node PoC-5 : JSN -> level -> Manning (v, Q) -> LoRa TX
 * =============================================================
 * Upgrade poc3_node: paket diperluas (11 byte) menjadi embrio FgvPacket:
 *   node_id | seq | level_mm | chan_mm | v_cms | q_lpm_x10 | crc8
 * - level_mm  : 0xFFFF = INVALID (aturan unknown != 0)
 * - v_cms     : kecepatan Manning (cm/s), 0 jika level INVALID
 * - q_lpm_x10 : debit Manning x10 (L/min), 0 jika level INVALID
 * - chan_mm   : tinggi dinding talang (konstanta terukur per node)
 *
 * Pola sensor = PoC-1 (median/5, INVALID >= 3/5) — tervalidasi.
 * Pola radio  = PoC-2 (SF9 BW125 CR4/5 sync 0xF1, CRC) — PDR 100%.
 * Manning     = flow_manning.h — konstanta dari geometri terukur 20 Sep.
 *
 * KONFIGURASI SAAT PEMASANGAN:
 *   Atur bracket sensor supaya jarak sensor->dasar talang (KERING) = 300 mm.
 *   Verifikasi: serial harus menampilkan dist ~300 mm saat talang kosong.
 *
 * ANTENA WAJIB TERPASANG SEBELUM TX.
 * Board: esp32:esp32:esp32s3 · Serial: 115200
 */

#include <LoRa.h>
#include <SPI.h>
#include <stdint.h>
#include "flow_manning.h"

// ---------------- Pin map (ARSITEKTUR §2.1) --------------------------------
#define LORA_NSS   10
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_RST   9
#define LORA_DIO0  8

const uint8_t PIN_TRIG = 5;
const uint8_t PIN_ECHO = 4;

// ---------------- Konstanta sensor (PoC-1, tervalidasi) ---------------------
const float SPEED_OF_SOUND_MPS = 343.42f;          // 20 degC
const float US_TO_MM           = SPEED_OF_SOUND_MPS / 2.0f / 1000.0f;
const uint8_t  N_SAMPLES         = 5;
const uint32_t PING_INTERVAL_MS  = 60;
const uint32_t ECHO_TIMEOUT_US   = 30000;
const float    MM_MIN            = 200.0f;
const float    MM_MAX            = 4500.0f;
const uint8_t  INVALID_TOLERANCE = 3;

// KONFIGURASI PER NODE — sesuaikan saat pasang bracket:
const float    SENSOR_BASE_MM = 300.0f;            // jarak sensor->dasar talang (kering)
const float    LEVEL_OFFSET_MM  = 0.0f;            // offset PoC-1 (+3.6 mm, kalau dipakai)
const uint16_t LEVEL_INVALID     = 0xFFFF;
const uint16_t Q_INVALID_X10     = 0xFFFF;

// ---------------- Geometri node (TERUKUR 20 Sep 2026) -----------------------
ManningConfig NODE = {
  .b_m     = 0.080f,      // 80 mm
  .S       = 0.00889f,    // 4 mm / 450 mm
  .n       = 0.010f,
  .chan_mm = 25.0f,
};

// ---------------- Radio (config final PoC-2) ---------------------------------
const long    RF_FREQ_HZ   = 433000000L;
const int     RF_SF        = 9;
const long    RF_BW        = 125E3;
const int     RF_CR        = 5;
const uint8_t RF_SYNCWORD = 0xF1;
const uint32_t TX_PERIOD_MS = 1000;
const uint8_t  NODE_ID     = 1;                     // 1=HULU

struct __attribute__((packed)) FgvPacketPoc5 {
  uint8_t  node_id;
  uint8_t  seq;
  uint16_t level_mm;       // 0xFFFF = INVALID
  uint16_t chan_mm;
  uint16_t v_cms;
  uint16_t q_lpm_x10;      // 0xFFFF = INVALID
  uint8_t  crc8;
};

uint8_t crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

// ---------------- Sensor (identik PoC-1) -------------------------------------
float takePingMm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  const uint32_t echo_us = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
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

// ---------------- State -------------------------------------------------------
uint8_t seq = 0;
uint32_t tx_ok = 0, tx_fail = 0, n_invalid = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  delay(500);
  Serial.println();
  Serial.println(F("FGV NODE PoC-5 | JSN + Manning + LoRa TX | paket 11B"));

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
  Serial.println(F("LoRa OK. Ukur level -> Manning v,Q -> TX 1x/dtk."));
  Serial.println(F("Cek pasang: talang KERING harus baca dist ~300 mm."));
}

void loop() {
  const uint32_t t0 = millis();

  // --- ukur: median dari 5 ---
  float valid[5];
  uint8_t n_valid = 0, n_bad = 0;
  for (uint8_t i = 0; i < 5; i++) {
    const float mm = takePingMm();
    if (isnan(mm)) n_bad++; else valid[n_valid++] = mm;
    if (i < 4) {
      const int32_t r = (int32_t)((i + 1) * PING_INTERVAL_MS)
                        - (int32_t)(millis() - t0);
      if (r > 0) delay((uint32_t)r);
    }
  }

  uint16_t level_tx = LEVEL_INVALID;
  float level_mm = NAN, v_cms = 0, q_lpm = 0;

  if (n_bad >= INVALID_TOLERANCE || n_valid < (5 - INVALID_TOLERANCE + 1)) {
    n_invalid++;
  } else {
    const float dist = medianOf(valid, n_valid) + LEVEL_OFFSET_MM;
    level_mm = SENSOR_BASE_MM - dist;                    // jarak -> level
    if (level_mm < 0)  level_mm = 0;                     // di bawah acuan dasar
    if (level_mm > NODE.chan_mm) level_mm = NODE.chan_mm;  // meluap: clamp
    v_cms = manning_v(NODE, level_mm) * 100.0f;
    q_lpm = manning_q_lpm(NODE, level_mm);
    level_tx = (uint16_t)(level_mm + 0.5f);
  }

  // --- TX ---
  FgvPacketPoc5 p;
  p.node_id   = NODE_ID;
  p.seq       = seq;
  p.chan_mm   = (uint16_t)NODE.chan_mm;
  if (level_tx == LEVEL_INVALID) {
    p.level_mm   = LEVEL_INVALID;
    p.v_cms      = 0;
    p.q_lpm_x10  = Q_INVALID_X10;
  } else {
    p.level_mm   = level_tx;
    p.v_cms      = (uint16_t)(v_cms + 0.5f);
    p.q_lpm_x10  = (uint16_t)(q_lpm * 10.0f + 0.5f);
  }
  p.crc8 = crc8((uint8_t *)&p, sizeof(p) - 1);

  LoRa.beginPacket();
  LoRa.write((uint8_t *)&p, sizeof(p));
  const int ok = LoRa.endPacket();
  if (ok == 1) tx_ok++; else tx_fail++;
  seq++;

  Serial.print(F("["));
  Serial.print(NODE_ID);
  Serial.print(F("] seq="));
  Serial.print(seq);
  if (level_tx == LEVEL_INVALID) {
    Serial.print(F("  level=INVALID"));
  } else {
    Serial.print(F("  lvl="));
    Serial.print(level_tx);
    Serial.print(F("mm  v="));
    Serial.print(v_cms, 1);
    Serial.print(F("cm/s  Q="));
    Serial.print(q_lpm, 1);
    Serial.print(F("L/min"));
  }
  Serial.print(F("  ok="));
  Serial.print(tx_ok);
  Serial.print(F(" fail="));
  Serial.println(tx_fail);

  const int32_t remain = (int32_t)TX_PERIOD_MS - (int32_t)(millis() - t0);
  if (remain > 0) delay((uint32_t)remain);
}