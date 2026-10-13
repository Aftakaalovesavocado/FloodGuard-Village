"""
FGV ENGINE v2 — Referensi logika inferensi FloodGuard Village
=============================================================
File ini adalah SUMBER KEBENARAN untuk logika inferensi. Firmware C++
(node_fgv.ino / gateway_fgv.ino) WAJIB menghasilkan angka yang sama persis
untuk input yang sama. Bila ada perubahan, ubah di sini dulu, validasi,
baru port ke C++.

Dua lapis:
  Layer 1 (per node)  : Fuzzy Sugeno 27 rule -> risk 0..100 + level 0/1/2
  Layer 2 (gateway)   : Spatial Pattern Classifier, 27 kombinasi -> kelas ancaman

Perubahan v2 (16 Sep 2026) dari MVP:
  1. MF hujan dikalibrasi ulang: mm/3jam (bukan mm/hari). MF lama tidak pernah
     aktif karena domainnya 10-60 mm sementara nilai BMKG nyata 0-4 mm.
  2. Input debit -> Indeks Debit I_Q = Q/Q_ref (rasio terhadap baseline),
     supaya rule base portabel antara testbed lab dan saluran lapangan.
  3. Estimasi laju kenaikan air: regresi linear least-squares (sesuai dokumen),
     menggantikan selisih titik ujung yang dipakai MVP.
"""

from math import sqrt

# ══════════════════════════════════════════════════════════════
# 1. FUNGSI KEANGGOTAAN
# ══════════════════════════════════════════════════════════════


def clamp(x, lo=0.0, hi=1.0):
    return lo if x < lo else (hi if x > hi else x)


def trap_left(x, b, c):
    """Bahasa: plateau 1.0 untuk x<=b, turun linear ke 0 di x=c."""
    if x <= b:
        return 1.0
    if x >= c:
        return 0.0
    return (c - x) / (c - b)


def trap_right(x, a, b):
    """Naik linear dari 0 di x=a ke 1.0 di x=b, plateau setelahnya."""
    if x <= a:
        return 0.0
    if x >= b:
        return 1.0
    return (x - a) / (b - a)


def triangle(x, a, b, c):
    if x <= a or x >= c:
        return 0.0
    if x == b:
        return 1.0
    if x < b:
        return (x - a) / (b - a)
    return (c - x) / (c - b)


# ── Variabel 1: ketinggian air (% kapasitas saluran) ──
# Dipertahankan dari MVP (sudah lolos 6 skenario uji tepi).
MF_LEVEL = {
    "rendah": (trap_left, 15.0, 35.0),
    "sedang": (triangle, 20.0, 45.0, 70.0),
    "tinggi": (trap_right, 50.0, 75.0),
}

# ── Variabel 2: Indeks Debit I_Q = Q / Q_ref ──
# BARU di v2. Rasio terhadap baseline terukur, bukan satuan absolut.
MF_FLOW = {
    "lambat": (trap_left, 0.8, 1.3),
    "sedang": (triangle, 1.0, 1.6, 2.6),
    "deras": (trap_right, 2.0, 3.0),
}

# ── Variabel 3: curah hujan dari BMKG, mm per periode prakiraan 3 jam ──
# BARU di v2 (terkalibrasi terhadap 140 sampel empiris, 7 wilayah).
MF_RAIN = {
    "ringan": (trap_left, 1.0, 2.5),
    "sedang": (triangle, 2.0, 4.0, 7.0),
    "lebat": (trap_right, 5.5, 10.0),
}

MF_ORDER = {  # urutan indeks di rule table: 0, 1, 2
    "level": ["rendah", "sedang", "tinggi"],
    "flow": ["lambat", "sedang", "deras"],
    "rain": ["ringan", "sedang", "lebat"],
}


def fuzzify(var, x):
    """Kembalikan list [mu_0, mu_1, mu_2] untuk variabel tertentu (var: 0=level, 1=flow, 2=rain)."""
    table = [MF_LEVEL, MF_FLOW, MF_RAIN][var]
    names = MF_ORDER[["level", "flow", "rain"][var]]
    return [clamp(table[n][0](x, *table[n][1:])) for n in names]


def fuzzify_named(var, x):
    table = [MF_LEVEL, MF_FLOW, MF_RAIN][var]
    names = MF_ORDER[["level", "flow", "rain"][var]]
    return {n: clamp(table[n][0](x, *table[n][1:])) for n in names}


# ══════════════════════════════════════════════════════════════
# 2. LAYER 1 — FUZZY SUGENO 27 RULE
# ══════════════════════════════════════════════════════════════

# Konsekuen zero-order. Indeks: [level][flow][rain].
# Diwarisi dari MVP (sudah lolos uji 6 skenario), SUDAH TIDAK DIUBAH.
RULE_OUTPUT = [
    [[5, 20, 50], [10, 30, 60], [20, 40, 70]],    # level RENDAH
    [[15, 40, 65], [35, 55, 75], [50, 70, 85]],   # level SEDANG
    [[40, 65, 85], [60, 80, 95], [80, 95, 100]],  # level TINGGI
]

RULE_COUNT = 27


def safety_reasons(level_pct, flow_idx):
    """
    Lapisan keselamatan (safety floor) — BARU di v2.

    Mengapa perlu: defuzzifikasi weighted-average hanya merata-ratakan rule yang
    MENYALA. Rule berkonsekuen tinggi yang tidak menyala tidak dapat menarik
    risiko ke atas, sehingga pada kasus ekstrem satu variabel saja (mis. saluran
    sudah penuh 100% tetapi debit rendah dan tidak hujan) risiko JENUH di nilai
    rendah. Contoh terukur: level 100%, I_Q 0.5, hujan ringan -> risk hanya 40
    (WASPADA), padahal air sudah di bibir saluran.

    Prinsip: logika fuzzy menangani bagian ANTISIPATIF (di bawah 75% kapasitas).
    Di atas ambang fisik tertentu, kondisi lapangan tidak lagi bisa
    dinegosiasikan oleh bobot fuzzy.
    """
    alasan = []
    if level_pct >= 90.0:
        alasan.append("muka air >= 90%% kapasitas (lantai risk 90)")
    if level_pct >= 75.0:
        alasan.append("muka air >= 75%% kapasitas (lantai risk 75 = BAHAYA)")
    if level_pct >= 60.0 and flow_idx >= 2.5:
        alasan.append("debit puncak (I_Q >= 2.5) dengan muka air tinggi (lantai risk 70)")
    return alasan


def apply_safety(risk, level_pct, flow_idx):
    lantai = 0.0
    if level_pct >= 90.0:
        lantai = max(lantai, 90.0)
    if level_pct >= 75.0:
        lantai = max(lantai, 75.0)
    if level_pct >= 60.0 and flow_idx >= 2.5:
        lantai = max(lantai, 70.0)
    return max(risk, lantai)


def risk_layer1(level_pct, flow_idx, rain_mm3h, verbose=False, apply_safety_layer=True):
    """Fuzzy Sugeno weighted-average -> risk 0..100 (sudah termasuk safety floor)."""
    mu_l = fuzzify(0, level_pct)
    mu_f = fuzzify(1, flow_idx)
    mu_r = fuzzify(2, rain_mm3h)

    num = 0.0
    den = 0.0
    fired = []
    for i in range(3):
        for j in range(3):
            for k in range(3):
                w = min(mu_l[i], mu_f[j], mu_r[k])
                if w > 0:
                    num += w * RULE_OUTPUT[i][j][k]
                    den += w
                    fired.append((i, j, k, w, RULE_OUTPUT[i][j][k]))
    risk = (num / den) if den else 0.0
    if apply_safety_layer:
        risk = apply_safety(risk, level_pct, flow_idx)
    if verbose:
        aktif = [(MF_ORDER["level"][i], MF_ORDER["flow"][j],
                  MF_ORDER["rain"][k], round(w, 3),
                  RULE_OUTPUT[i][j][k]) for i, j, k, w, _ in fired]
        return risk, aktif
    return risk, fired


def status_4level(risk):
    """Status untuk ditampilkan ke warga (4 tingkat)."""
    if risk >= 75:
        return "BAHAYA"
    if risk >= 50:
        return "SIAGA"
    if risk >= 25:
        return "WASPADA"
    return "AMAN"


def level_l1(risk):
    """Kategorisasi 3 tingkat untuk pattern classifier (input Layer 2)."""
    if risk >= 60:
        return 2   # Tinggi
    if risk >= 25:
        return 1   # Sedang
    return 0       # Rendah


# ══════════════════════════════════════════════════════════════
# 3. LAYER 2 — SPATIAL PATTERN CLASSIFIER (27 KOMBINASI)
# ══════════════════════════════════════════════════════════════

KELAS = {
    "AMAN":          "Semua node pada level Rendah",
    "KIRIMAN_HULU":  "Banjir kiriman dari hulu (gelombang bergerak ke hilir)",
    "BACKWATER":     "Pembendungan / sumbatan di hilir (efek backwater)",
    "LOKAL":         "Banjir lokal pada satu segmen",
    "SERENTAK":      "Kenaikan merata sepanjang saluran",
    "CAMPURAN":      "Pola tidak cocok dengan signature manapun - perlu verifikasi lapangan",
}

AKSI = {
    "AMAN":         "Monitoring rutin",
    "KIRIMAN_HULU": "Peringatkan warga hilir, amankan barang, siapkan jalur evakuasi",
    "BACKWATER":    "Kirim tim cek fisik sumbatan/bendung di hilir",
    "LOKAL":        "Cek segmen terkait, siapkan pompa lokal",
    "SERENTAK":     "Aktivasi posko siaga desa",
    "CAMPURAN":     "Verifikasi manual lapangan sebelum mengambil keputusan",
}


def classify_pattern(h, t, d, iq_hilir=None, iq_hilir_base=1.0, level_hilir=None):
    """
    h, t, d = level_l1 node Hulu, Tengah, Hilir (0/1/2).

    Presedensi sengaja berurutan dan eksplisit. Urutan inilah yang
    'membentuk' 27 rule dari prinsip yang bisa dijelaskan, bukan tabel
    hafalan. Tabel hasil dapat diaudit lewat enumerate_pattern_table().

    Return: (kelas, catatan_konfirmasi)
    """
    mx = max(h, t, d)
    if mx == 0:
        return "AMAN", None

    # (1) gradien menurun sempurna hulu->hilir = gelombang banjir kiriman
    if h > t > d:
        return "KIRIMAN_HULU", None

    # (2) gradien menaik sempurna hulu->hilir = kenaikan terarah ke hilir
    if d > t > h:
        return "BACKWATER", konfirmasi_backwater(iq_hilir, iq_hilir_base, level_hilir)

    # (3) hilir dominan (tidak monoton) = indikasi sumbatan hilir
    if d > h and d >= t:
        return "BACKWATER", konfirmasi_backwater(iq_hilir, iq_hilir_base, level_hilir)

    # (4) hulu dominan (tidak monoton) = gelombang masih dalam perjalanan
    if h > d and h >= t:
        return "KIRIMAN_HULU", None

    # (5) puncak tunggal di tengah
    if t > h and t > d:
        return "LOKAL", None

    # (6) ketiga node setara levelnya
    if h == t == d:
        return "SERENTAK", None

    # (7) hanya satu node naik
    if (1 if h >= 1 else 0) + (1 if t >= 1 else 0) + (1 if d >= 1 else 0) == 1:
        return "LOKAL", None

    # (8) sisanya: pola tidak dikenal
    return "CAMPURAN", None


def konfirmasi_backwater(iq_hilir, iq_hilir_base, level_hilir):
    """
    Implementasi konkret prinsip dokumen:
    'hilir naik TAPI debit tidak proporsional naik = indikasi air tertahan'.
    Muka air naik sementara indeks debit turun -> sumbatan kuat.
    """
    if iq_hilir is None or iq_hilir_base in (None, 0):
        return None
    rasio = iq_hilir / iq_hilir_base
    lv = level_hilir if level_hilir is not None else None
    lv_txt = ("%.0f%%" % (lv * 100 if lv <= 1.0 else lv)) if lv is not None else "n/a"
    if rasio < 0.85:
        return ("indikasi sumbatan KUAT: muka air hilir %s sementara debit turun "
                "ke %.2fx baseline" % (lv_txt, rasio))
    if rasio <= 1.05:
        return "debit stagnan (%.2fx baseline) - konsisten dengan pembendungan" % rasio
    return "debit justru naik (%.2fx baseline) - pertimbangkan ulang penyebabnya" % rasio


def enumerate_pattern_table():
    """Bangkitkan seluruh 3^3 = 27 kombinasi -> daftar dict."""
    baris = []
    for h in (2, 1, 0):
        for t in (2, 1, 0):
            for d in (2, 1, 0):
                kelas, _ = classify_pattern(h, t, d)
                baris.append({"hulu": h, "tengah": t, "hilir": d, "kelas": kelas})
    return baris


# ══════════════════════════════════════════════════════════════
# 4. ESTIMASI WAKTU MELUAP (Least-Squares)
# ══════════════════════════════════════════════════════════════


def slope_ls(level_samples, t_seconds):
    """
    Kemiringan regresi linear least-squares, satuan %/detik.
    Menggantikan selisih titik ujung MVP yang rentan satu sampel berisik.
    """
    n = len(level_samples)
    if n < 3 or n != len(t_seconds):
        return None
    mx = sum(t_seconds) / n
    my = sum(level_samples) / n
    sxx = sum((x - mx) ** 2 for x in t_seconds)
    if sxx == 0:
        return None
    sxy = sum((t_seconds[i] - mx) * (level_samples[i] - my) for i in range(n))
    return sxy / sxx


def time_to_overflow(level_samples, t_seconds, level_now=None,
                     min_samples=3, slope_deadband=0.0005):
    """
    Return (menit_ke_luapan | None, alasan).
    slope_deadband = 0.0005 %/detik (~0.03%/menit) -> di bawah ini dianggap datar.
    """
    if len(level_samples) < min_samples:
        return None, "sample kurang (%d/%d)" % (len(level_samples), min_samples)
    m = slope_ls(level_samples, t_seconds)
    if m is None:
        return None, "data tidak valid"
    if m <= slope_deadband:
        return None, "stabil / surut"
    now = level_now if level_now is not None else level_samples[-1]
    sisa = 100.0 - now
    if sisa <= 0:
        return 0.0, "sudah meluap"
    menit = (sisa / m) / 60.0
    return menit, "ok"


# ══════════════════════════════════════════════════════════════
# 5. PERSAMAAN MANNING & GEOMETRI SALURAN
# ══════════════════════════════════════════════════════════════


def manning(b_mm, m_side, s_slope, n_manning, h_mm):
    """
    Penampang trapesium. Semua panjang dalam mm, hasil debit L/menit.
    b_mm   : lebar dasar saluran (mm)
    m_side : kemiringan dinding horizontal:vertikal (0 = persegi)
    s_slope: kemiringan dasar saluran (m/m)
    h_mm   : tinggi muka air (mm)
    """
    if h_mm <= 0:
        return 0.0, 0.0
    b = b_mm / 1000.0
    h = h_mm / 1000.0
    A = b * h + m_side * h * h
    P = b + 2.0 * h * sqrt(1.0 + m_side ** 2)
    if P <= 0:
        return 0.0, 0.0
    R = A / P
    v = (1.0 / n_manning) * (R ** (2.0 / 3.0)) * sqrt(s_slope)
    q_m3s = A * v
    return v, q_m3s * 60000.0   # m/s , L/menit


# ══════════════════════════════════════════════════════════════
# 6. CRC8 untuk paket LoRa (harus identik di firmware)
# ══════════════════════════════════════════════════════════════


def crc8(data: bytes, poly=0x07, init=0x00) -> int:
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc
