/*
 * fuzzy_layer1.h — Layer 1 Fuzzy Sugeno 27 rule (port 1:1 dari sim/fgv_engine.py)
 * ===============================================================================
 * SUMBER KEBENARAN: fgv_engine.py v2 (16 Sep 2026). Konstanta di bawah HARUS
 * identik dengan Python — perubahan hanya boleh lewat Python dulu, validasi,
 * baru port. Review angka: lihat tabel side-by-side di chat 21 Sep 2026.
 *
 * Input : level_pct (% kapasitas saluran), flow_idx I_Q = Q/Q_ref, rain mm/3jam
 * Output: risk 0..100 (termasuk safety floor), status 4 tingkat, level_l1 0/1/2
 *
 * CATATAN v2 (fgv_engine.py): MF hujan satuan mm/3jam (bukan mm/hari — bug MVP);
 * debit pakai rasio I_Q (portabel testbed <-> lapangan); safety floor mencegah
 * risiko jenuh rendah saat kondisi fisik ekstrem (level>=75% dst).
 *
 * Header murni — bisa di-include oleh node ATAU gateway. Prototipe: dieksekusi
 * di gateway (per node). Relokasi ke node = include + wiring data, nol logika.
 */

#ifndef FUZZY_LAYER1_H
#define FUZZY_LAYER1_H

#include <stdint.h>
#include <math.h>

static inline float f1_clamp(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// trap_left: plateau 1 utk x<=b, turun linear ke 0 di c
static inline float f1_trap_left(float x, float b, float c) {
  if (x <= b) return 1.0f;
  if (x >= c) return 0.0f;
  return (c - x) / (c - b);
}
// trap_right: naik 0->1 dari a ke b, plateau
static inline float f1_trap_right(float x, float a, float b) {
  if (x <= a) return 0.0f;
  if (x >= b) return 1.0f;
  return (x - a) / (b - a);
}
static inline float f1_triangle(float x, float a, float b, float c) {
  if (x <= a || x >= c) return 0.0f;
  if (x == b) return 1.0f;
  return (x < b) ? (x - a) / (b - a) : (c - x) / (c - b);
}

// ---- MF: 3 variabel x 3 label. Format {tipe, p1, p2(, p3)} ------------------
// tipe 0=trap_left(b,c) 1=trap_right(a,b) 2=triangle(a,b,c)
struct F1Mf { uint8_t tipe; float p1, p2, p3; };

// LEVEL: rendah TL(15,35) | sedang TRI(20,45,70) | tinggi TR(50,75)
static const F1Mf F1_LEVEL[3] = {
  {0, 15.0f, 35.0f, 0}, {2, 20.0f, 45.0f, 70.0f}, {1, 50.0f, 75.0f, 0}
};
// FLOW I_Q: lambat TL(0.8,1.3) | sedang TRI(1.0,1.6,2.6) | deras TR(2.0,3.0)
static const F1Mf F1_FLOW[3] = {
  {0, 0.8f, 1.3f, 0}, {2, 1.0f, 1.6f, 2.6f}, {1, 2.0f, 3.0f, 0}
};
// RAIN mm/3jam: ringan TL(1.0,2.5) | sedang TRI(2.0,4.0,7.0) | lebat TR(5.5,10)
static const F1Mf F1_RAIN[3] = {
  {0, 1.0f, 2.5f, 0}, {2, 2.0f, 4.0f, 7.0f}, {1, 5.5f, 10.0f, 0}
};

static inline float f1_mu(const F1Mf &m, float x) {
  switch (m.tipe) {
    case 0:  return f1_clamp(f1_trap_left(x, m.p1, m.p2));
    case 1:  return f1_clamp(f1_trap_right(x, m.p1, m.p2));
    default: return f1_clamp(f1_triangle(x, m.p1, m.p2, m.p3));
  }
}

// ---- Konsekuen zero-order RULE_OUTPUT[level][flow][rain] --------------------
static const uint8_t F1_RULE_OUTPUT[3][3][3] = {
  // level RENDAH: [rain ringan, sedang, lebat] x flow
  { { 5, 20, 50}, {10, 30, 60}, {20, 40, 70} },
  // level SEDANG
  { {15, 40, 65}, {35, 55, 75}, {50, 70, 85} },
  // level TINGGI
  { {40, 65, 85}, {60, 80, 95}, {80, 95, 100} },
};

// ---- Safety floor (fgv_engine.apply_safety) ---------------------------------
static inline float f1_safety_floor(float level_pct, float flow_idx) {
  float lantai = 0.0f;
  if (level_pct >= 90.0f) lantai = 90.0f;                                  // >=90%
  if (level_pct >= 75.0f && lantai < 75.0f) lantai = 75.0f;                // >=75%
  if (level_pct >= 60.0f && flow_idx >= 2.5f && lantai < 70.0f) lantai = 70.0f;
  return lantai;
}

// ---- Layer 1 utama -----------------------------------------------------------
// Keluar: risk 0..100 (weighted average + safety floor)
static inline float f1_risk(float level_pct, float flow_idx, float rain_mm3h) {
  const float mu_l[3] = { f1_mu(F1_LEVEL[0], level_pct),
                          f1_mu(F1_LEVEL[1], level_pct),
                          f1_mu(F1_LEVEL[2], level_pct) };
  const float mu_f[3] = { f1_mu(F1_FLOW[0], flow_idx),
                          f1_mu(F1_FLOW[1], flow_idx),
                          f1_mu(F1_FLOW[2], flow_idx) };
  const float mu_r[3] = { f1_mu(F1_RAIN[0], rain_mm3h),
                          f1_mu(F1_RAIN[1], rain_mm3h),
                          f1_mu(F1_RAIN[2], rain_mm3h) };

  float num = 0, den = 0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) {
        const float w = fminf(mu_l[i], fminf(mu_f[j], mu_r[k]));
        if (w > 0.0f) {
          num += w * (float)F1_RULE_OUTPUT[i][j][k];
          den += w;
        }
      }
  float risk = (den > 0.0f) ? (num / den) : 0.0f;
  const float lantai = f1_safety_floor(level_pct, flow_idx);
  return (risk > lantai) ? risk : lantai;
}

// Status 4 tingkat utk warga: 0=AMAN 1=WASPADA 2=SIAGA 3=BAHAYA
static inline uint8_t f1_status4(float risk) {
  if (risk >= 75.0f) return 3;
  if (risk >= 50.0f) return 2;
  if (risk >= 25.0f) return 1;
  return 0;
}
static const char *F1_STATUS_NAME[4] = { "AMAN", "WASPADA", "SIAGA", "BAHAYA" };

// Kategorisasi utk Layer 2: 0=Rendah 1=Sedang(>=25) 2=Tinggi(>=60)
static inline uint8_t f1_level_l1(float risk) {
  if (risk >= 60.0f) return 2;
  if (risk >= 25.0f) return 1;
  return 0;
}

#endif // FUZZY_LAYER1_H
