#!/usr/bin/env python3
"""Turns recordings of a real SP-1200 or SP-12 into a calibration file for
the library.

Recording, with any audio interface and any sample rate:

1. Make the kit with make_kit.py. Sample kit_noise.wav into the machine at a
   level that peaks just short of clipping, and assign the sound to a pad on
   each of the eight output channels. Sample kit_tone.wav the same way and
   assign it to a pad on channel 8.
2. NOISE recording, two channels. For each output from 1 to 8 in turn, plug
   a stereo (TRS) cable into that output's jack, tip to the interface's left
   input and ring to its right, play the noise pad once and let it finish,
   then leave at least a second before the next. Outputs 7 and 8 have only a
   tip; the right channel stays silent for them. On an SP-12, whose jacks are
   mono, use a mono cable throughout.
3. TONES recording, one channel. With output 8 into the interface, play the
   tone pad 16 times, at tunings -8, -7 and so on up to +7, letting each
   finish and leaving half a second between them.
4. Run: analyse.py NOISE.wav TONES.wav [-o machine.cal] [--machine sp12]

It prints what it measured and writes the calibration file, which the md12
tool reads with -k. Needs build/probe (make cli).
"""
import argparse
import os
import sys

import numpy as np
import scipy.signal as ss

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import measure as M  # noqa: E402

TABLE = [50, 53, 56, 59, 63, 67, 71, 75, 80, 84, 90, 95, 101, 107, 113, 120,
         127, 135, 143, 151, 160, 170, 180, 191, 202, 214, 227, 241, 255, 255, 255, 255]


def response_db(tip, ring, fs, n=4096):
    """The ring over the tip, from their cross-spectrum."""
    f, pxy = ss.csd(tip, ring, fs, nperseg=n)
    f, pxx = ss.welch(tip, fs, nperseg=n)
    return f, 20 * np.log10(np.abs(pxy) / pxx + 1e-12)


def noise_take(path, machine):
    fs, x = M.load(path)
    tip = x[:, 0]
    ring = x[:, 1] if x.shape[1] > 1 else None
    level = 0.05 * np.abs(tip).max()
    segs = M.bursts(tip, fs, threshold=level, gap=0.5)
    if len(segs) != 8:
        raise SystemExit(f"analyse: found {len(segs)} sounds in {path}, expected 8")
    out = {}
    if ring is None or machine == "sp12":
        print("  mono jacks: the filters are measured against output 8")
    for k, (a, b) in enumerate(segs):
        o = k + 1
        a0 = M.onset(tip, a, level)
        if ring is not None and machine != "sp12" and o <= 6:
            filt, raw = ring[a0:b], tip[a0:b]
        elif o <= 6:
            a8 = M.onset(tip, segs[7][0], level)
            n = min(b - a0, segs[7][1] - a8)
            filt, raw = tip[a0:a0 + n], tip[a8:a8 + n]
        else:
            continue
        out[o] = (filt, raw)
    return fs, out


def tones_take(path):
    fs, x = M.load(path)
    m = x[:, 0]
    segs = M.bursts(m, fs, threshold=0.05 * np.abs(m).max(), gap=0.3, shortest=0.2)
    if len(segs) != 16:
        raise SystemExit(f"analyse: found {len(segs)} tones in {path}, expected 16")
    freqs = []
    for a, b in segs:
        seg = m[a + (b - a) // 4:b - (b - a) // 4]
        n = 1 << int(np.ceil(np.log2(len(seg)) + 2))
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
        k = int(np.argmax(spec[1:])) + 1
        y0, y1, y2 = np.log(spec[k - 1:k + 2] + 1e-30)
        k += 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
        freqs.append(k * fs / n)
    return np.array(freqs) / freqs[8]


def main():
    ap = argparse.ArgumentParser(description="SP-1200 or SP-12 recordings to a calibration file")
    ap.add_argument("noise")
    ap.add_argument("tones")
    ap.add_argument("-o", "--out", default="machine.cal")
    ap.add_argument("--machine", default="sp1200", choices=["sp1200", "sp12"])
    a = ap.parse_args()
    print(f"analyse: {a.noise} and {a.tones}, as an {a.machine.upper()}")

    ratios = tones_take(a.tones)
    rules = {1: [(TABLE[i] + 1) / 128 for i in range(8, 24)],
             0: [((TABLE[i] | 1) + 1) / 128 for i in range(8, 24)]}
    err = {k: float(np.sqrt(np.mean(np.log(ratios / (np.array(r) / r[8])) ** 2))) for k, r in rules.items()}
    lsb = 1 if err[1] < err[0] else 0
    print(f"pitch: (byte + 1) / 128 misses by {100 * err[1]:.3f}% rms, the rule without the "
          f"lowest bit by {100 * err[0]:.3f}%; pitch_lsb {lsb}")

    fs, outs = noise_take(a.noise, a.machine)
    scales = []
    for o in (3, 4, 5, 6):
        filt, raw = outs[o]
        f, r = response_db(raw, filt, fs)
        sc, res = M.fixed_scale(f, r, o)
        scales.append(sc)
        print(f"output {o}: fixed filter at {sc:.3f} of the schematic's frequencies, residual {res:.2f} dB")
    rest, gain, pin, cap = [0, 0], [1, 1], [], [1, 1]
    for o in (1, 2):
        filt, raw = outs[o]
        late = int(0.7 * fs)
        f, r = response_db(raw[late:], filt[late:], fs)
        fp, g, res = M.rest_pole(f, r)
        rest[o - 1], gain[o - 1] = fp, 10 ** (g / 20)
        sw = M.sweep(filt, raw, fs)
        e, ohms, c = M.fit_sweep(sw, a.machine, fp)
        pin.append(ohms)
        cap[o - 1] = c
        print(f"output {o}: rests at {fp:.0f} Hz, gain {g:+.2f} dB, residual {res:.2f} dB; sweep fits "
              f"a pin 13 load of {ohms:.0f} ohms and C111 at {c:.3f}, within {e:.3f} octaves rms")

    with open(a.out, "w") as fh:
        fh.write(f"# written by tools/kit/analyse.py from {os.path.basename(a.noise)} and "
                 f"{os.path.basename(a.tones)}\n")
        fh.write(f"machine {a.machine}\n")
        fh.write(f"pitch_lsb {lsb}\n")
        fh.write("fixed_scale " + " ".join(f"{s:.4f}" for s in scales) + "\n")
        fh.write(f"dyn_rest_hz {rest[0]:.1f} {rest[1]:.1f}\n")
        fh.write(f"dyn_pin13_ohms {np.mean(pin):.0f}\n")
        fh.write(f"dyn_cap_scale {cap[0]:.4f} {cap[1]:.4f}\n")
        fh.write(f"dyn_gain {gain[0]:.4f} {gain[1]:.4f}\n")
    print(f"analyse: wrote {a.out}")


if __name__ == "__main__":
    main()
