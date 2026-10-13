/*
 * pattern_layer2.h — Layer 2 Spatial Pattern Classifier (port 1:1 fgv_engine.py)
 * ==============================================================================
 * 27 kombinasi (level_l1 hulu,tengah,hilir) -> kelas ancaman.
 * Presedensi berurutan (bukan tabel hafalan): tiap aturan punya alasan fisik.
 * Konfirmasi backwater: hilir naik + I_Q hilir tidak naik proporsional
 *   = indikasi air tertahan/sumbatan (prinsip ARSITEKTUR par.6).
 */

#ifndef PATTERN_LAYER2_H
#define PATTERN_LAYER2_H

#include <stdint.h>

enum L2Kelas : uint8_t {
  L2_AMAN = 0, L2_KIRIMAN_HULU, L2_BACKWATER, L2_LOKAL, L2_SERENTAK, L2_CAMPURAN
};

static const char *L2_KELAS_NAME[6] = {
  "AMAN", "KIRIMAN_HULU", "BACKWATER", "LOKAL", "SERENTAK", "CAMPURAN"
};
static const char *L2_KELAS_DESC[6] = {
  "Semua node level Rendah",
  "Banjir kiriman dari hulu (gelombang ke hilir)",
  "Pembendungan/sumbatan di hilir (backwater)",
  "Banjir lokal satu segmen",
  "Kenaikan merata sepanjang saluran",
  "Pola tak dikenal - verifikasi lapangan"
};
static const char *L2_AKSI[6] = {
  "Monitoring rutin",
  "Peringatkan warga hilir, siapkan evakuasi",
  "Kirim tim cek sumbatan/bendung hilir",
  "Cek segmen terkait, siapkan pompa lokal",
  "Aktivasi posko siaga desa",
  "Verifikasi manual sebelum keputusan"
};

// Konfirmasi backwater dari rasio I_Q_hilir / baseline.
// rasio<0.85 = sumbatan kuat; <=1.05 stagnan (konsisten); >1.05 debit naik.
// Return: 0=tanpa info 1=kuat 2=stagnan 3=debit naik
static inline uint8_t l2_konfirmasi_backwater(float iq_hilir, float iq_base) {
  if (iq_base <= 0.0f) return 0;
  const float rasio = iq_hilir / iq_base;
  if (rasio < 0.85f)  return 1;
  if (rasio <= 1.05f) return 2;
  return 3;
}

// h, t, d = level_l1 (0/1/2) node Hulu/Tengah/Hilir.
// Keluar via pointer: kelas + flag konfirmasi (0..3, hanya utk BACKWATER).
static inline void l2_classify(uint8_t h, uint8_t t, uint8_t d,
                               float iq_hilir, float iq_base,
                               L2Kelas *kelas, uint8_t *konfirmasi) {
  *konfirmasi = 0;

  const uint8_t mx = (h > t) ? (h > d ? h : d) : (t > d ? t : d);
  if (mx == 0) { *kelas = L2_AMAN; return; }                    // (0) semua 0

  if (h > t && t > d) { *kelas = L2_KIRIMAN_HULU; return; }     // (1) turun sempurna
  if (d > t && t > h) { *kelas = L2_BACKWATER; *konfirmasi = l2_konfirmasi_backwater(iq_hilir, iq_base); return; } // (2) naik sempurna
  if (d > h && d >= t) { *kelas = L2_BACKWATER; *konfirmasi = l2_konfirmasi_backwater(iq_hilir, iq_base); return; } // (3) hilir dominan
  if (h > d && h >= t) { *kelas = L2_KIRIMAN_HULU; return; }    // (4) hulu dominan
  if (t > h && t > d) { *kelas = L2_LOKAL; return; }            // (5) puncak tengah
  if (h == t && t == d) { *kelas = L2_SERENTAK; return; }       // (6) setara
  { // (7) hanya satu node naik -> LOKAL
    const uint8_t naik = (h >= 1) + (t >= 1) + (d >= 1);
    if (naik == 1) { *kelas = L2_LOKAL; return; }
  }
  *kelas = L2_CAMPURAN;                                         // (8) sisanya
}

#endif // PATTERN_LAYER2_H
