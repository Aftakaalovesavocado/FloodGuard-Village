/*
 * FGV PROTOTIPE — PoC-3 : End-to-end mini
 * JSN-SR04T (3.3V) → median/5 → level_mm → paket CRC8 → LoRa TX
 * =====================================================================
 * Gabungan poc1_sensor (pemenang LULUS, galat +3.6 mm offset) dan
 * poc2_tx_node (PDR 100%). Pin tidak bentrok:
 *   JSN: TRIG=5, ECHO=4 (3.3V path)  ·  LoRa: NSS=10 SCK=12 MISO=13 MOSI=11 RST=9 DIO0=8
 *
 * Paket 5 byte (packed): node_id | seq | level_mm (u16) | crc8
 *   level_mm = 0xFFFF berarti INVALID (aturan: unknown ≠ 0).
 *   Offset kalibrasi per sensor: LEVEL_OFFSET_MM (PoC-1 terukur +3.6 mm;
 *   konstanta disiapkan, default 0 sampai keputusan kalibrasi final).
 *
 * Radio persis poc2 (tervalidasi bench, PDR 100%): 433 MHz, SF9, BW125,
 * CR4/5, sync 0xF1, CRC on.
 *
 * ⚠️ ANTENA WAJIB TERPASANG SEBELUM SKETCH INI JALAN.
 * Board: esp32:esp32:esp32s3 · Serial: 115200
 */

#include <LoRa.h>
#include <SPI.h>
#include <stdint.h>

// ---------------- Pin map (ARSITEKTUR §2.1) -------------------------------
#define LORA_NSS   10
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_RST   9
#define LORA_DIO0  8

const uint8_t PIN_TRIG = 5;   // JSN trigger, 3.3V aman
const uint8_t PIN_ECHO = 4;   // JSN echo, 3.3V path (WIRING.md PoC-1)

// ---------------- Konstanta sensor (keputusan PoC-1) -----------------------
const float SPEED_OF_SOUND_MPS = 343.42f;        // 20 degC
const float US_TO_MM           = SPEED_OF_SOUND_MPS / 2.0f / 1000.0f;
const uint8_t  N_SAMPLES         = 5;
const uint32_t PING_INTERVAL_MS  = 60;
const uint32_t ECHO_TIMEOUT_US   = 30000;
const float    MM_MIN            = 200.0f;
const float    MM_MAX            = 4500.0f;
const uint8_t  INVALID_TOLERANCE = 3;
const float    LEVEL_OFFSET_MM   = 0.0f;        // kalibrasi per sensor (±3.6 mm)
const uint16_t LEVEL_INVALID     = 0xFFFF;

// ---------------- Radio (identik poc2, tervalidasi) ------------------------
const long    RF_FREQ_HZ   = 433000000L;
const int     RF_SF        = 9;
const long    RF_BW        = 125E3;
const int     RF_CR        = 5;
const uint8_t RF_SYNCWORD = 0xF1;
const uint32_t TX_PERIOD_MS = 1000;

const uint8_t NODE_ID = 1;      // 1=HULU (PoC: satu node saja)

struct __attribute__((packed)) Poc3Tx {
  uint8_t  node_id;
  uint8_t  seq;
  uint16_t level_mm;      // 0xFFFF = INVALID
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

// ---------------- Sensor ----------------------------------------------------
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

// ---------------- State -----------------------------------------------------
uint8_t seq = 0;
uint32_t tx_ok = 0, tx_fail = 0, n_invalid_cycles = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  delay(500);
  Serial.println();
  Serial.println(F("FGV PoC-3 NODE | JSN(3.3V,median/5) + LoRa TX | paket 5B CRC8"));

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
  Serial.println(F("LoRa OK. Siklus: ukur 5 ping -> median -> TX tiap 1 dtk."));
}

void loop() {
  const uint32_t t0 = millis();

  // --- ukur: median dari 5 (identik poc1) ---
  float valid[N_SAMPLES];
  uint8_t n_valid = 0, n_invalid = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    const float mm = takePingMm();
    if (isnan(mm)) n_invalid++; else valid[n_valid++] = mm;
    if (i < N_SAMPLES - 1) {
      const int32_t r = (int32_t)((i + 1) * PING_INTERVAL_MS)
                        - (int32_t)(millis() - t0);
      if (r > 0) delay((uint32_t)r);
    }
  }

  uint16_t level_tx;
  float level_reported;
  if (n_invalid >= INVALID_TOLERANCE ||
      n_valid < (N_SAMPLES - INVALID_TOLERANCE + 1)) {
    level_tx = LEVEL_INVALID;                       // unknown ≠ 0
    level_reported = NAN;
    n_invalid_cycles++;
  } else {
    level_reported = medianOf(valid, n_valid) + LEVEL_OFFSET_MM;
    level_tx = (uint16_t)(level_reported + 0.5f);
  }

  // --- TX ---
  Poc3Tx p;
  p.node_id  = NODE_ID;
  p.seq      = seq;
  p.level_mm = level_tx;
  p.crc8     = crc8((uint8_t *)&p, sizeof(p) - 1);

  LoRa.beginPacket();
  LoRa.write((uint8_t *)&p, sizeof(p));
  const int ok = LoRa.endPacket();
  if (ok == 1) tx_ok++; else tx_fail++;

  Serial.print(F("["));
  Serial.print(NODE_ID);
  Serial.print(F("] seq="));
  Serial.print(seq);
  if (isnan(level_reported)) {
    Serial.print(F("  level=INVALID ("));
    Serial.print(n_invalid);
    Serial.print(F("/5 ping gagal)"));
  } else {
    Serial.print(F("  level_mm="));
    Serial.print(level_tx);
  }
  Serial.print(F("  TX="));
  Serial.print(ok == 1 ? F("OK") : F("FAIL"));
  Serial.print(F("  ok="));
  Serial.print(tx_ok);
  Serial.print(F(" fail="));
  Serial.println(tx_fail);
  seq++;

  const int32_t remain = (int32_t)TX_PERIOD_MS - (int32_t)(millis() - t0);
  if (remain > 0) delay((uint32_t)remain);
}
