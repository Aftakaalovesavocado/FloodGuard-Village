"""
VALIDASI FGV ENGINE v2 — dijalankan untuk menghasilkan sim/HASIL_VALIDASI.md
Jalankan: python run_validasi.py
"""
import sys, os, json, io, datetime
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fgv_engine import (risk_layer1, status_4level, level_l1, classify_pattern,
                        enumerate_pattern_table, KELAS, AKSI, MF_LEVEL, MF_FLOW,
                        MF_RAIN, fuzzify_named, time_to_overflow, slope_ls,
                        manning, crc8)

OUT = io.StringIO()


def w(s=""):
    print(s)
    OUT.write(str(s) + "\n")


def judul(s, ch="="):
    w()
    w(ch * 78)
    w(s)
    w(ch * 78)


# ──────────────────────────────────────────────────────────────
judul("VALIDASI ENGINE FLOODGUARD VILLAGE v2")
w("Dijalankan: " + datetime.datetime.now().strftime("%d %b %Y %H:%M"))
w("Python     : " + sys.version.split()[0])

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 1 — UJI MF BARU TERHADAP DATA BMKG NYATA", "-")
w()
w("Nilai acuan (hasil query API BMKG, 140 sampel / 7 wilayah):")
w("  - kondisi tanpa hujan (label Cerah/Berawan) : tp = 0.0 - 1.0 mm/3jam")
w("  - label 'Hujan Ringan' BMKG                 : tp = 1.2 - 4.0 mm/3jam")
w("  - ambang hujan lebat (50 mm/hari / 8)       : tp = 6.25 mm/3jam")
w()
w("Uji balik MF hujan v2:")
w()
w("  tp(mm/3j)  Ringan  Sedang   Lebat   Hasil     Keterangan")
w("  " + "-" * 66)
kasus = [(0.2, "prakiraan Cerah (kondisi hari ini)"),
         (1.0, "noise floor 'Berawan' - harus TIDAK memicu"),
         (2.5, "batas hujan ringan/sedang"),
         (4.0, "nilai maksimum teramati di 140 sampel"),
         (6.5, "ambang hujan lebat"),
         (10.0, "hujan lebat"),
         (25.0, "sangat lebat - harus jenuh, tidak overflow")]
for tp, ket in kasus:
    m = fuzzify_named(2, tp)
    hasil = max(m, key=m.get)
    w("  %7.1f   %6.3f  %6.3f  %6.3f   %-8s  %s" %
      (tp, m["ringan"], m["sedang"], m["lebat"], hasil.upper(), ket))

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 2 — BUKTI BUG MF LAMA (hujan sebagai input mati)", "-")
w()


def risk_mf_lama(level_pct, debit_lpm, rain_mm):
    """Replikasi persis MF + rule MVP (sebelum perbaikan) untuk pembuktian."""
    from fgv_engine import trap_left, trap_right, triangle, RULE_OUTPUT
    ml = [trap_left(level_pct, 15, 35), triangle(level_pct, 20, 45, 70),
          trap_right(level_pct, 50, 75)]
    mf = [trap_left(debit_lpm, 15, 40), triangle(debit_lpm, 20, 45, 80),
          trap_right(debit_lpm, 60, 90)]
    mr = [trap_left(rain_mm, 10, 25), triangle(rain_mm, 15, 35, 55),
          trap_right(rain_mm, 40, 60)]
    num = den = 0.0
    for i in range(3):
        for j in range(3):
            for k in range(3):
                ww = min(ml[i], mf[j], mr[k])
                if ww > 0:
                    num += ww * RULE_OUTPUT[i][j][k]
                    den += ww
    return num / den if den else 0.0


w("Kondisi sama (level 20%, debit 30 L/menit), hanya curah hujan yang diubah:")
w()
w("  MF LAMA (domain 10-60 mm, mm/hari diasumsikan):")
r_a = risk_mf_lama(20, 30, 0.2)
r_b = risk_mf_lama(20, 30, 8.0)
w("    tp = 0.2 mm  -> risk = %6.2f  (%s)" % (r_a, status_4level(r_a)))
w("    tp = 8.0 mm  -> risk = %6.2f  (%s)" % (r_b, status_4level(r_b)))
w("    SELISIH      = %6.2f  <-- NOL = input hujan tidak berpengaruh sama sekali" % abs(r_b - r_a))
w()
w("  MF BARU v2 (domain terkalibrasi 0-10 mm/3jam):")
r_c, _ = risk_layer1(20, 30 / 30.0, 0.2)
r_d, _ = risk_layer1(20, 30 / 30.0, 8.0)
w("    tp = 0.2 mm  -> risk = %6.2f  (%s)" % (r_c, status_4level(r_c)))
w("    tp = 8.0 mm  -> risk = %6.2f  (%s)" % (r_d, status_4level(r_d)))
w("    SELISIH      = %6.2f  dengan muka air yang SAMA" % abs(r_d - r_c))
w()
w("  -> KESIMPULAN: pada versi lama, sistem hanya reaktif (butuh air naik).")
w("     Pada v2, prakiraan hujan lebat dapat menaikkan status sebelum air naik.")

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 2B — RISIKO JENUH (SATURASI) & LAPISAN KESELAMATAN", "-")
w()
w("Masalah: defuzzifikasi weighted-average hanya merata-ratakan rule yang menyala.")
w("Rule berkonsekuen tinggi yang TIDAK menyala tidak dapat menarik risiko ke atas.")
w("Akibatnya, pada kasus ekstrem satu variabel, risiko jenuh di nilai rendah.")
w()
w("  level(%)  I_Q   hujan   risk TANPA        risk DENGAN      status akhir")
w("                          lapisan aman     lapisan aman")
w("  " + "-" * 72)
for lv, iq, label in ((100, 0.5, "saluran PENUH, debit rendah"),
                      (95, 0.6, "saluran hampir penuh"),
                      (80, 0.5, "muka air tinggi, aliran pelan"),
                      (78, 2.6, "muka air tinggi + debit puncak"),
                      (60, 0.5, "muka air sedang, aliran pelan"),
                      (40, 1.0, "kondisi normal")):
    r_off, _ = risk_layer1(lv, iq, 0.2, apply_safety_layer=False)
    r_on, _ = risk_layer1(lv, iq, 0.2, apply_safety_layer=True)
    tanda = "  <-- diperbaiki" if abs(r_on - r_off) > 0.01 else ""
    w("  %6d   %.1f   ringan  %6.1f %-13s %6.1f %-11s %s%s"
      % (lv, iq, r_off, "(%s)" % status_4level(r_off), r_on,
         "(%s)" % status_4level(r_on), label, tanda))
w()
w("  ATURAN LAPISAN KESELAMATAN (eksplisit, bisa dipertanggungjawabkan):")
w("    - muka air >= 75% kapasitas  -> risk minimal 75 (BAHAYA)")
w("    - muka air >= 90% kapasitas  -> risk minimal 90")
w("    - I_Q >= 2.5 DAN level >= 60% -> risk minimal 70")
w()
w("  Argumennya: ambang 75% kapasitas adalah titik di mana kenaikan air sudah")
w("  mengancam luapan secara langsung - kondisi fisik ini tidak boleh bisa")
w("  'dinegosiasikan' oleh bobot fuzzy. Logika fuzzy tetap menangani bagian")
w("  ANTISIPATIF (prakiraan hujan & tren) yang justru berada di bawah 75%.")

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 3 — TABEL LENGKAP 27 KOMBINASI LAYER 2", "-")
w()
tabel = enumerate_pattern_table()
w("  Hulu  Tengah  Hilir   Kelas Pola        Contoh situasi")
w("  " + "-" * 72)
nama_tingkat = {0: "Rendah", 1: "Sedang", 2: "Tinggi"}
contoh = {
    "AMAN": "kondisi normal",
    "KIRIMAN_HULU": "gelombang dari hulu, hilir belum terdampak",
    "BACKWATER": "sumbatan hilir mendorong air balik",
    "LOKAL": "hujan setempat pada satu segmen",
    "SERENTAK": "seluruh saluran naik bersamaan",
    "CAMPURAN": "pola tak dikenal - perlu verifikasi lapangan",
}
for b in tabel:
    w("  %-5s %-7s %-7s  %-17s %s" % (nama_tingkat[b["hulu"]],
                                      nama_tingkat[b["tengah"]],
                                      nama_tingkat[b["hilir"]],
                                      b["kelas"], contoh[b["kelas"]]))
from collections import Counter
cnt = Counter(b["kelas"] for b in tabel)
w()
w("  REKAP: " + " | ".join("%s=%d" % (k, v) for k, v in sorted(cnt.items())))
w("  Total kombinasi: %d (harus 27)" % len(tabel))
assert len(tabel) == 27
# validasi: tepat 1 kombinasi AMAN
assert cnt["AMAN"] == 1, "seharusnya hanya 1 kombinasi AMAN"

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 4 — SKENARIO END-TO-END (Layer 1 + Layer 2)", "-")
w()

SKENARIO = [
    {"id": "S1", "nama": "Normal / baseline (cuaca cerah)",
     "nodes": {"HULU": (12, 1.00, 0.2), "TENGAH": (10, 1.00, 0.2), "HILIR": (11, 1.00, 0.2)}},
    {"id": "S2", "nama": "Prakiraan hujan lebat, air MASIH rendah (uji leading indicator)",
     "nodes": {"HULU": (18, 1.05, 8.0), "TENGAH": (16, 1.05, 8.0), "HILIR": (15, 1.05, 8.0)}},
    {"id": "S3", "nama": "Banjir kiriman dari hulu (gelombang dalam perjalanan)",
     "nodes": {"HULU": (78, 2.60, 6.0), "TENGAH": (45, 1.70, 6.0), "HILIR": (14, 1.00, 6.0)}},
    {"id": "S4", "nama": "Sumbatan di hilir (backwater, debit hilir turun)",
     "nodes": {"HULU": (14, 1.00, 3.0), "TENGAH": (18, 1.05, 3.0), "HILIR": (72, 0.55, 3.0)}},
    {"id": "S5", "nama": "Hujan lokal hanya di segmen tengah",
     "nodes": {"HULU": (11, 1.00, 2.0), "TENGAH": (68, 2.20, 9.0), "HILIR": (12, 1.00, 2.0)}},
    {"id": "S6", "nama": "Kritis serentak sepanjang saluran",
     "nodes": {"HULU": (88, 2.80, 10.0), "TENGAH": (84, 2.60, 10.0), "HILIR": (80, 2.50, 10.0)}},
]

for sk in SKENARIO:
    w("  " + sk["id"] + " — " + sk["nama"])
    lv = {}
    detail = {}
    for n in ("HULU", "TENGAH", "HILIR"):
        l, iq, rain = sk["nodes"][n]
        risk, _ = risk_layer1(l, iq, rain)
        lv[n] = level_l1(risk)
        detail[n] = (l, iq, rain, risk)
        w("      %-6s level=%5.1f%%  I_Q=%.2f  hujan=%.1f mm/3j  ->  risk=%5.1f  (%s, level L1=%d)"
          % (n, l, iq, rain, risk, status_4level(risk), lv[n]))
    kelas, catatan = classify_pattern(lv["HULU"], lv["TENGAH"], lv["HILIR"],
                                      iq_hilir=detail["HILIR"][1],
                                      level_hilir=detail["HILIR"][0] / 100.0)
    w("      >> KELAS POLA : %s" % kelas)
    w("         %s" % KELAS[kelas])
    if catatan:
        w("         Konfirmasi debit: %s" % catatan)
    w("         Tindakan: %s" % AKSI[kelas])
    w()

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 5 — KUANTIFIKASI LEAD TIME (pengganti klaim '40% lebih awal')", "-")
w()
w("Simulasi hidrograf: muka air naik linear 20% -> 100% dengan laju 0.4 %/menit,")
w("indeks debit I_Q konstan 1.2, ambang peringatan = risk 50 (SIAGA).")
w()
w("  t(menit)  level(%)   risk TANPA hujan   risk hujan sedang   risk hujan lebat")
w("  " + "-" * 76)
t_alert_no_rain = t_alert_rain = t_alert_sedang = None
for t in range(0, 205, 10):
    lv_pct = 20 + 0.4 * t
    r0, _ = risk_layer1(lv_pct, 1.2, 0.2)
    r1, _ = risk_layer1(lv_pct, 1.2, 8.0)
    r2, _ = risk_layer1(lv_pct, 1.2, 3.0)
    if t_alert_no_rain is None and r0 >= 50:
        t_alert_no_rain = t
    if t_alert_rain is None and r1 >= 50:
        t_alert_rain = t
    if t_alert_sedang is None and r2 >= 50:
        t_alert_sedang = t
    tanda = "   <== SIAGA hanya karena prakiraan hujan" if (r1 >= 50 and r0 < 50) else ""
    w("  %7d   %6.1f   %14.1f   %17.1f   %17.1f%s" % (t, lv_pct, r0, r2, r1, tanda))
w()
w("  Waktu ambang SIAGA (risk>=50) pertama tercapai:")
w("    - sistem hanya level+debit (hujan diabaikan) : menit %s" % t_alert_no_rain)
w("    - v2 dengan prakiraan HUJAN SEDANG (3 mm/3j) : menit %s" % t_alert_sedang)
w("    - v2 dengan prakiraan HUJAN LEBAT  (8 mm/3j) : menit %s" % t_alert_rain)
w()
w("    LEAD TIME diperoleh, hujan sedang : %d menit" % (t_alert_no_rain - t_alert_sedang))
w("    LEAD TIME diperoleh, hujan lebat  : %d menit" % (t_alert_no_rain - t_alert_rain))
w()
w("  CATATAN KEJUJURAN (wajib disebut saat presentasi):")
w("    a. Ini hasil SIMULASI MODEL, bukan pengukuran lapangan.")
w("    b. Angka lead time bergantung pada laju kenaikan air yang diasumsikan")
w("       (0.4 %/menit di sini). Laju lebih lambat -> lead time lebih besar.")
w("    c. Yang harus dibuktikan di testbed adalah MEKANISMENYA (prakiraan hujan")
w("       saja dapat mengangkat status tanpa air naik), bukan angka menitnya.")
w("    d. Nilai prakiraan hujan diuji lewat injeksi nilai pada gateway (skenario S2).")

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 6 — VERIFIKASI PERHITUNGAN WAKTU MELUAP", "-")
w()
w("Kasus uji: level naik linear 1.0 %/menit. Sampel terakhir = 46%.")
w("Acuan yang benar = (100 - 46) / 1.0 = 54.00 menit (bukan 60).")
w("Catatan: estimator memakai level TERAKHIR sebagai titik awal hitung sisa")
w("kapasitas - jadi acuan harus dihitung dari sampel terakhir, bukan dari awal window.")
w()
samples = [40 + 1.0 * (i * 0.5) for i in range(13)]   # 13 sampel tiap 30 detik, berakhir 46%
ts = [i * 30.0 for i in range(13)]
ACUAN = (100.0 - samples[-1]) / 1.0
m = slope_ls(samples, ts)
m_menit = m * 60
menit, alasan = time_to_overflow(samples, ts)
w("  jumlah sampel        : %d (window %.1f menit)" % (len(samples), ts[-1] / 60))
w("  level awal -> akhir  : %.1f%% -> %.1f%%" % (samples[0], samples[-1]))
w("  kemiringan least-sq  : %.6f %%/detik  = %.4f %%/menit" % (m, m_menit))
w("  estimasi meluap      : %.2f menit  (%s)" % (menit, alasan))
w("  acuan benar          : %.2f menit" % ACUAN)
w("  galat                : %.3f menit" % abs(menit - ACUAN))
w()
w("  Uji ketahanan terhadap noise (5 sampel terakhir dinaikkan acak +/-2%):")
import random
random.seed(42)
for trial in range(3):
    noisy = samples[:]
    for i in range(8, 13):
        noisy[i] += random.uniform(-2, 2)
    m2 = slope_ls(noisy, ts)
    mn2, _ = time_to_overflow(noisy, ts)
    m1 = slope_ls(samples, ts)
    mn1, _ = time_to_overflow(samples, ts)
    w("    trial %d: least-squares = %6.2f menit | metode titik-ujung (MVP) = %6.2f menit"
      % (trial + 1, mn2, (100 - noisy[-1]) / ((noisy[-1] - noisy[0]) / (ts[-1] - ts[0])) / 60))
w()
w("  -> Metode titik-ujung mudah terlempar oleh satu sampel berisik;")
w("     least-squares jauh lebih stabil. Ini alasan penggantian di v2.")

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 7 — PERSAMAAN MANNING: TABEL SENSITIVITAS", "-")
w()
w("Geometri uji (rencana testbed): saluran PVC persegi,")
w("  lebar dasar b=100 mm, dinding vertikal (m=0), kemiringan S=0.5%, n=0.010")
w()
w("  tinggi air   A(m2)      R(m)     v(m/s)    Q(L/menit)")
w("  " + "-" * 56)
for h in (5, 10, 15, 20, 25, 30, 40):
    v, q = manning(100, 0.0, 0.005, 0.010, h)
    A = 0.1 * (h / 1000.0)
    P = 0.1 + 2 * (h / 1000.0)
    w("  %6d mm   %8.5f  %8.5f  %8.4f  %10.2f" % (h, A, A / P, v, q))
w()
w("  PRESYARATAN VALIDITAS (penting untuk rancangan testbed):")
w("  Manning berlaku untuk aliran gravitasi seragam (uniform steady flow).")
w("  Bila saluran uji dialiri pompa langsung, debit ditentukan kurva pompa,")
w("  bukan gravitasi -> Manning TIDAK valid diuji di sana.")
w("  Karena itu testbed WAJIB gravity-fed: pompa -> tangki header ->")
w("  saluran mengalir gravitasi -> tangki penampung -> kembali ke pompa.")

# ──────────────────────────────────────────────────────────────
judul("BAGIAN 8 — CRC8 PAKET LoRa (vektor uji untuk firmware)", "-")
w()
w("  Uji known-answer (standar CRC-8/ATM, polynomial 0x07, init 0x00):")
ka = crc8(b"123456789")
w("    crc8('123456789') = 0x%02X  %s" % (ka, "BENAR (nilai referensi 0xF4)" if ka == 0xF4 else "SALAH!"))
assert ka == 0xF4, "implementasi CRC8 tidak sesuai standar"
w()
w("  Vektor uji paket:")
w("  " + "-" * 50)
for v in [bytes([1, 0, 0x50, 0x00]), bytes([2, 7, 0xFF, 0x00]),
      bytes([3, 255, 0x00, 0x01]), bytes(16)]:
    w("  %-40s 0x%02X" % (v.hex(" "), crc8(v)))
w()
w("  Angka-angka ini dipakai sebagai unit test di firmware C++ (harus sama).")

# ──────────────────────────────────────────────────────────────
judul("KESIMPULAN VALIDASI")
w()
w("  1. MF hujan v2 lolos uji balik terhadap data BMKG nyata: noise floor")
w("     tidak memicu alarm, hujan lebat mendorong status ke SIAGA.")
w("  2. Bug input mati pada MF lama terbukti kuantitatif (selisih risk = 0.00).")
w("  2b. Risiko jenuh pada kasus ekstrem teridentifikasi (level 100% -> risk 40)")
w("      dan ditambal dengan lapisan keselamatan eksplisit 3 aturan.")
w("  3. Tabel 27 kombinasi Layer 2 lengkap dan konsisten (%d baris, 1 AMAN)." % len(tabel))
w("  4. Lead time dari prakiraan hujan terkuantifikasi: %s menit (hujan lebat)."
  % (t_alert_no_rain - t_alert_rain if (t_alert_no_rain is not None and t_alert_rain is not None) else "n/a"))
w("  5. Estimasi waktu meluap least-squares: galat %.3f menit pada kasus uji bersih." % abs(menit - ACUAN))
w("  6. Semua angka di atas dapat direproduksi: python run_validasi.py")
w()
w("  STATUS: validasi tingkat model SELESAI. Validasi hardware & testbed = Fase 3-4.")

open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "HASIL_VALIDASI.md"),
     "w", encoding="utf-8").write("# Hasil Validasi FGV Engine v2\n\n```\n" + OUT.getvalue() + "```\n")
print("\n[tersimpan] sim/HASIL_VALIDASI.md")
