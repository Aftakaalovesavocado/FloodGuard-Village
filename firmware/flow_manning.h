/*
 * flow_manning.h — Estimasi debit Manning on-chip (FGV prototipe)
 * ================================================================
 * Sumber kebenaran: ARSITEKTUR_DAN_PROTOKOL.md par.5 + geometri
 * testbed terukur 20 Sep 2026:
 *   Saluran talang: panjang 45 cm, lebar 8 cm, tinggi dinding 2.5 cm
 *   Elevasi hulu 593 mm, hilir 589 mm -> S = 4/450 = 0.00889
 *   n = 0.010 (permukaan logam seng/lembut, tabel standar Manning;
 *   akan dikalibrasi ulang vs debit volumetrik — Fase 4)
 *
 * Semua fungsi menerima level mm dari dasar saluran dan mengembalikan
 * v (m/s) dan Q (L/min). Level 0 atau INVALID tidak boleh dipanggil
 * ke sini (validasi dilakukan pemanggil — aturan unknown ≠ 0).
 *
 * Verifikasi hitung (20 Sep): h=15mm @S=0.00889 -> Q=33.4 L/min
 * = kapasitas pompa 2000 L/h -> level operasi stabil ~15 mm. Cocok.
 */

#ifndef FLOW_MANNING_H
#define FLOW_MANNING_H

#include <stdint.h>
#include <math.h>

// ---------------- Geometri saluran (TERUKUR 20 Sep 2026) ------------------
#define CHANNEL_WIDTH_M     0.080f     // b  = 8 cm
#define CHANNEL_HEIGHT_MM   25.0f      // tinggi dinding talang
#define CHANNEL_SLOPE       0.00889f   // S  = 4mm / 450mm (terukur)
// n EFEKTIF TERKALIBRASI (21 Sep): titik (h=9mm, Q volumetrik 9.98 L/min,
// 10 repetisi CV 4.5%) -> n = 0.010 x 15.39/9.98 = 0.0154. Masuk akal utk
// dasar plat bordes (buku teks: logam bergelombang 0.016-0.022).
// Kalibrasi 1 titik; 2 titik tambahan menyusul -> lihat KALIBRASI_MANNING.md
#define MANNING_N           0.0154f

// Kembalikan debit L/min (integer x10 biar muat di paket u16)
// level_mm: tinggi muka air dari DASAR saluran
// Q_lpm_x10 keluar 0..65535; panggil hanya bila level valid & > 0
static inline void manningFromLevelMm(float level_mm,
                                      float *v_out, float *Q_lpm_out) {
  const float b = CHANNEL_WIDTH_M;
  const float h = level_mm / 1000.0f;
  if (h <= 0.0f) { *v_out = 0; *Q_lpm_out = 0; return; }

  const float A = b * h;                       // penampang basah (m2)
  const float P = b + 2.0f * h;                // keliling basah (m)
  const float R = A / P;                       // jari-jari hidrolis (m)
  const float v = (1.0f / MANNING_N) * powf(R, 2.0f / 3.0f)
                  * sqrtf(CHANNEL_SLOPE);
  *v_out     = v;
  *Q_lpm_out = A * v * 1000.0f * 60.0f;        // L/min
}

// Versi integer utk paket LoRa (Q x10, saturasi 65535)
static inline uint16_t manningQx10(float level_mm) {
  float v, Q;
  manningFromLevelMm(level_mm, &v, &Q);
  const float qx10 = Q * 10.0f;
  if (qx10 >= 65535.0f) return 65535;
  return (uint16_t)(qx10 + 0.5f);
}

#endif // FLOW_MANNING_H
