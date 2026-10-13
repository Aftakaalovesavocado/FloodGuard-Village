/*
 * FGV PROTOTIPE — PoC-4 : Bench test CDM324 (24.125 GHz Doppler) + LM358
 * =======================================================================
 * STATUS: EKSPERIMEN OPSIONAL — BUKAN JALUR DEMO.
 * Dokumen risiko PLAN_ROADMAP.md §3: "CDM324 tidak bisa diandalkan;
 * Manning jadi jalur utama; CDM324 hanya bench test terpisah."
 *
 * FISIKA (verifikasi sim): lambda = 12.43 mm -> f_d = 2v/lambda
 *   v 0.05 m/s -> 8 Hz   | v 0.2 m/s -> 32 Hz  | v 1 m/s -> 161 Hz
 *   Testbed (Q 3-10 L/min, pipa 40x40): f_d ~ 3-34 Hz — bertabrakan
 *   dengan hum PLN 50 Hz. Makanya analisis pakai FFT di PC, bukan
 *   cuma zero-crossing di perangkat.
 *
 * TINGKAT KEBERHASILAN (saling bertingkat):
 *   T1: RMS & spektrum berubah saat ada gerakan (tangan/kipas) vs diam
 *   T2: f_d dominan sebanding kecepatan target berputar (RPM terukur)
 *   T3: (Fase 4, belakangan) korelasi dengan debit volumetrik air
 *
 * WIRING (lihat WIRING.md): IF -> GPIO 7 (ADC1_CH6, sesuai ARSITEKTUR §2.1)
 *   Varian A: modul CDM324 dengan amplifier onboard -> IF langsung ke GPIO 7
 *   Varian B: IF mentah -> LM358 gain ~100, bias 1.65V -> GPIO 7
 *
 * MODE serial (115200):
 *   (tanpa perintah) : tiap 1 dtk print "t=.. rms=.. zc=.."
 *   'd'              : dump satu jendela 4000 sampel CSV (2 detik @2kHz)
 *                      untuk FFT di PC (analyze_doppler.py)
 *
 * Board: esp32:esp32:esp32s3 · ADC: analogReadResolution(12)
 */

#include <stdint.h>

const uint8_t PIN_IF = 7;        // CDM324 IF (envelope), sesuai pin map FGV
const uint32_t FS_HZ       = 2000;   // sampling rate untuk dump
const uint16_t N_DUMP      = 4000;   // 2 detik @ 2 kHz
const uint32_t STAT_PERIOD = 1000;   // statistik tiap 1 dtk

// Window statistik berkelanjutan (biar responsif di serial)
const uint16_t N_STAT = 1000;        // 0.5 dtk @ 2 kHz internal pacing

uint32_t t_stat = 0;

// ---- sampling stat window: RMS + zero crossing (dengan histeresis) ------
void sampleStats(float &rms, float &zc_hz) {
  const int16_t HYST = 20;                 // histeresis ADC count
  uint32_t sum_sq = 0;
  float mean = 0;

  // pass 1: mean (DC level = titik bias)
  for (uint16_t i = 0; i < N_STAT; i++) {
    mean += (float)analogRead(PIN_IF);
    delayMicroseconds(1000000UL / FS_HZ - 8);   // pacing kasar ~2 kHz
  }
  mean /= N_STAT;

  // pass 2: RMS + zero crossing dgn histeresis
  uint16_t crossings = 0;
  bool above = false;
  for (uint16_t i = 0; i < N_STAT; i++) {
    const float x = (float)analogRead(PIN_IF) - mean;
    sum_sq += (uint32_t)(x * x);
    const bool now_above = (x > HYST);
    if (now_above != above) { crossings++; above = now_above; }
    delayMicroseconds(1000000UL / FS_HZ - 8);
  }
  rms = sqrtf((float)sum_sq / N_STAT);
  zc_hz = (float)crossings / 2.0f / ((float)N_STAT / FS_HZ);
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_IF, ADC_11db);   // range penuh ~0-3.3V
  delay(500);
  Serial.println();
  Serial.println(F("FGV PoC-4 | CDM324 Doppler bench | IF=GPIO7 fs=2kHz"));
  Serial.println(F("Kirim 'd' untuk dump 4000 sampel CSV (untuk FFT di PC)."));
  Serial.println(F("Uji T1: (1) catat 10 dtk SEMUA DIAM, (2) goyang tangan/kipas 10 dtk."));
  t_stat = millis();
}

void loop() {
  // dump mode: satu jendela CSV
  if (Serial.available() && Serial.read() == 'd') {
    Serial.println(F("i,adc"));
    for (uint16_t i = 0; i < N_DUMP; i++) {
      Serial.print(i);
      Serial.print(',');
      Serial.println(analogRead(PIN_IF));
      delayMicroseconds(1000000UL / FS_HZ - 8);
    }
    Serial.println(F("# dump selesai"));
  }

  // statistik berkelanjutan tiap 1 dtk
  if (millis() - t_stat >= STAT_PERIOD) {
    t_stat = millis();
    float rms, zc_hz;
    sampleStats(rms, zc_hz);
    Serial.print(F("t="));
    Serial.print(millis() / 1000);
    Serial.print(F("s  rms="));
    Serial.print(rms, 1);
    Serial.print(F(" cnt  zc~"));
    Serial.print(zc_hz, 1);
    Serial.println(F(" Hz"));
  }
}
