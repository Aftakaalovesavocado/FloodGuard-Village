/*
 * flow_manning.h — Persamaan Manning on-chip (FGV)
 * =================================================
 * Sumber kebenaran: ARSITEKTUR_DAN_PROTOKOL.md par.5
 *   A(h) = b*h                    (saluran kotak, m = 0)
 *   P(h) = b + 2*h
 *   R(h) = A/P
 *   v    = (1/n) * R^(2/3) * S^(1/2)
 *   Q    = A * v
 *
 * Geometri terukur testbed (20 Sep 2026, mistar/waterpass):
 *   b      = 80 mm   (lebar dasar talang)
 *   chan   = 25 mm   (tinggi dinding)
 *   L      = 450 mm, delta hulu-hilir = 4 mm (593 vs 589) -> S = 0.00889
 *   n      = 0.010   (PVC/metal licin, tabel standar)
 *
 * Konstanta per node diisi lewat ManningConfig (dari shared config nanti).
 */

#pragma once
#include <math.h>

struct ManningConfig {
  float b_m;        // lebar dasar (m)
  float S;          // kemiringan dasar (m/m)
  float n;          // koefisien Manning
  float chan_mm;    // tinggi maksimum saluran (mm)
};

// Kedalaman h dalam mm -> luas basah (m^2)
static inline float manning_area_m2(const ManningConfig &c, float h_mm) {
  const float h = h_mm / 1000.0f;
  return c.b_m * h;
}

// v (m/s) dari kedalaman h (mm)
static inline float manning_v(const ManningConfig &c, float h_mm) {
  const float h = h_mm / 1000.0f;
  if (h <= 0.0005f) return 0.0f;             // di bawah 0.5 mm: anggap kering
  const float A = c.b_m * h;
  const float P = c.b_m + 2.0f * h;
  const float R = A / P;
  return (1.0f / c.n) * powf(R, 2.0f / 3.0f) * sqrtf(c.S);
}

// Debit Q (L/menit) dari kedalaman h (mm)
static inline float manning_q_lpm(const ManningConfig &c, float h_mm) {
  const float h = h_mm / 1000.0f;
  if (h <= 0.0005f) return 0.0f;
  const float A = c.b_m * h;
  const float v = manning_v(c, h_mm);
  return A * v * 60000.0f;      // m^3/s -> L/min
}