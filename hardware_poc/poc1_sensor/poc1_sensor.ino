/*
 * FGV PROTOTIPE — PoC-1 : Sensor JSN-SR04T + ESP32-S3
 * =====================================================
 * Scope (disepakati 18 Sep 2026):
 *   - Kecepatan suara: konstanta 20 degC (343.4 m/s). Suhu ruang dicatat
 *     manual saat kalibrasi; tanpa sensor suhu. Galat karakteristik ~1.3%
 *     per 7 degC, konsisten -> masuk offset kalibrasi.
 *   - Median dari 5 ping, jeda antar-ping >= 60 ms.
 *   - Aturan invalid: >= 3 dari 5 ping invalid -> output INVALID.
 *     Nilai "tidak diketahui" TIDAK PERNAH dikonversi menjadi 0
 *     (aturan integritas data ARSITEKTUR_DAN_PROTOKOL.md par.4).
 *   - Validasi 3x10 (30/50/80 cm) ditunda; PoC ini fokus sensor + filter.
 *
 * Wiring (lihat WIRING.md di folder ini):
 *   JSN VCC  -> VIN (5V, saat board di-USB)
 *   JSN GND  -> GND
 *   JSN TRIG -> GPIO 5  (3.3V, aman langsung)
 *   JSN ECHO -> GPIO 4  lewat pembagi 1k/2k dari 5V (5.0 x 2/3 = 3.33 V)
 *
 * Catatan: blind zone JSN-SR04T ~25-30 cm. Jarak < MM_MIN dianggap
 * invalid, bukan jarak nyata. Saat dipasang di saluran: level_mm =
 * tinggi_pasang_sensor - jarak_terbaca (dikerjakan di firmware final).
 *
 * Board: esp32:esp32:esp32s3 (DevKitC-1)
 * Serial: 115200 baud
 */

#include <stdint.h>

// ---------------- Pin map (sesuai ARSITEKTUR_DAN_PROTOKOL.md par.2.1) ----
const uint8_t PIN_TRIG = 5;   // output 3.3V, aman
const uint8_t PIN_ECHO = 4;   // WAJIB pembagi 1k/2k (level ECHO 5V)

// ---------------- Konstanta akustik (keputusan desain #1) ---------------
// v = 331.3 + 0.606*T  ->  T = 20 degC : 343.42 m/s
const float SPEED_OF_SOUND_MPS = 343.42f;
// round-trip: mm per mikrodetik = (v m/s) / 2 / 1000
const float US_TO_MM = SPEED_OF_SOUND_MPS / 2.0f / 1000.0f;  // 0.17171

// ---------------- Filter & aturan valid (keputusan desain #2) ------------
const uint8_t  N_SAMPLES          = 5;     // median of 5
const uint32_t PING_INTERVAL_MS   = 60;    // biar echo lama mati
const uint32_t ECHO_TIMEOUT_US    = 30000; // ~4.5 m round trip -> timeout
const float    MM_MIN             = 200.0f;   // bawah blind zone (25 cm + margin)
const float    MM_MAX             = 4500.0f;  // batas spesifikasi JSN
const uint8_t  INVALID_TOLERANCE  = 3;     // >= 3 invalid -> siklus INVALID
const uint32_t CYCLE_PERIOD_MS    = 1000;  // 1 laporan per detik

// Set 1 untuk debug: print kelima sampel mentah tiap siklus
#define POC1_PRINT_RAW 0

// ---------------- State ---------------------------------------------------
uint32_t cycle_start_ms = 0;

// Satu ping: kirim trigger 10 us, ukur lebar pulsa ECHO (us).
// Mengembalikan jarak dalam mm, atau NAN bila invalid.
float takePingMm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  const uint32_t echo_us = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (echo_us == 0) return NAN;                       // timeout: tak ada echo
  const float mm = (float)echo_us * US_TO_MM;
  if (mm < MM_MIN || mm > MM_MAX) return NAN;         // di luar window wajar
  return mm;
}

// Median dari larik nilai valid (n = jumlah sampel valid)
float medianOf(float *v, uint8_t n) {
  // insertion sort — n <= 5, tak perlu qsort
  for (uint8_t i = 1; i < n; i++) {
    const float key = v[i];
    int8_t j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  delay(500);                       // beri JSN stabil pasca power-up
  Serial.println();
  Serial.println(F("FGV PoC-1 | JSN-SR04T | v_suara=343.42 m/s (20C) | median/5"));
}

void loop() {
  cycle_start_ms = millis();

  float valid[N_SAMPLES];
  uint8_t n_valid = 0;
  uint8_t n_invalid = 0;

#if POC1_PRINT_RAW
  Serial.print(F("raw: ["));
#endif

  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    const float mm = takePingMm();
    if (isnan(mm)) {
      n_invalid++;
#if POC1_PRINT_RAW
      Serial.print(F("x"));
#endif
    } else {
      valid[n_valid++] = mm;
#if POC1_PRINT_RAW
      Serial.print(mm, 1);
      Serial.print(F(" "));
#endif
    }
#if POC1_PRINT_RAW
    if (i == N_SAMPLES - 1) Serial.println(F("]"));
#endif
    // jeda antar-ping; potong sisa waktu siklus di akhir, bukan di sini
    if (i < N_SAMPLES - 1) {
      const uint32_t next_ping = millis() - cycle_start_ms
                                 + (uint32_t)(i + 1) * PING_INTERVAL_MS;
      const int32_t remain = (int32_t)next_ping - (int32_t)millis();
      if (remain > 0) delay((uint32_t)remain);
    }
  }

  // ---- Aturan invalid: >= 3 dari 5 -> INVALID (bukan 0, bukan tebakan) --
  if (n_invalid >= INVALID_TOLERANCE || n_valid < (N_SAMPLES - INVALID_TOLERANCE + 1)) {
    Serial.print(F("t="));
    Serial.print(millis());
    Serial.print(F(" ms  INVALID  ("));
    Serial.print(n_invalid);
    Serial.print(F("/"));
    Serial.print(N_SAMPLES);
    Serial.println(F(" ping invalid)"));
  } else {
    const float mm = medianOf(valid, n_valid);
    Serial.print(F("t="));
    Serial.print(millis());
    Serial.print(F(" ms  dist_mm="));
    Serial.print(mm, 1);
    Serial.print(F("  valid="));
    Serial.print(n_valid);
    Serial.print(F("/"));
    Serial.println(N_SAMPLES);
  }

  // Laporan tepat 1x per detik
  const int32_t remain = (int32_t)CYCLE_PERIOD_MS - (int32_t)(millis() - cycle_start_ms);
  if (remain > 0) delay((uint32_t)remain);
}
