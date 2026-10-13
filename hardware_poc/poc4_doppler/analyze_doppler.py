#!/usr/bin/env python3
"""Analyzer PoC-4: FFT dari dump CSV poc4_doppler.ino.

Pakai:
    python analyze_doppler.py dump.csv
CSV format: baris "i,adc" + komentar '# ...'. fs=2000 Hz.
Output: frekuensi dominan 1-500 Hz + RMS + grafik spectrum.png.
"""
import csv
import sys

import numpy as np

FS = 2000.0
BAND_LO, BAND_HI = 1.0, 500.0   # Hz — band sinyal Doppler yang relevan


def load(path):
    xs = []
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0].startswith("#") or row[0] == "i":
                continue
            xs.append(float(row[1]))
    return np.array(xs)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    x = load(sys.argv[1])
    x = x - x.mean()          # buang DC (titik bias)
    n = len(x)
    t = np.arange(n) / FS

    rms = np.sqrt(np.mean(x ** 2))
    spec = np.abs(np.fft.rfft(x * np.hanning(n)))
    freqs = np.fft.rfftfreq(n, 1 / FS)
    band = (freqs >= BAND_LO) & (freqs <= BAND_HI)
    f_band, s_band = freqs[band], spec[band]
    f_dom = f_band[np.argmax(s_band)] if len(f_band) else 0

    print(f"Sampel        : {n} ({n/FS:.2f} s @ {FS:.0f} Hz)")
    print(f"RMS           : {rms:.1f} count ADC")
    print(f"f dominan     : {f_dom:.1f} Hz  (band {BAND_LO:.0f}-{BAND_HI:.0f} Hz)")
    # konversi Doppler -> kecepatan (HANYA bila sumber benar-benar bergerak
    # searah berkas; angka ini tidak boleh diklaim sebagai kecepatan air
    # tanpa validasi volumetrik — aturan dokumen par.5)
    lam = 299_792_458.0 / 24.125e9
    print(f"v ekuivalen   : {f_dom*lam/2*100:.1f} cm/s  (DOPPLER, BUKAN kecepatan air)")

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(2, 1, figsize=(9, 6))
        ax[0].plot(t[:2000], x[:2000], lw=0.5)
        ax[0].set_title("IF envelope (2 dtk pertama)")
        ax[0].set_xlabel("s")
        ax[1].plot(f_band, s_band, lw=0.8)
        ax[1].axvline(50, color="r", ls="--", lw=0.8, label="50 Hz hum PLN")
        ax[1].set_xlim(0, 300)
        ax[1].set_title(f"Spektrum — dominan {f_dom:.1f} Hz")
        ax[1].set_xlabel("Hz")
        ax[1].legend()
        fig.tight_layout()
        fig.savefig("spectrum.png", dpi=120)
        print("Grafik        : spectrum.png")
    except Exception as e:  # matplotlib opsional
        print(f"(grafik dilewati: {e})")


if __name__ == "__main__":
    main()
