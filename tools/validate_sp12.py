#!/usr/bin/env python3
"""Checks the library against a real SP-12: John Nolting's CCRMA recordings,
https://ccrma.stanford.edu/~nolting/220c/, sine_sweep_all_ch_tune.wav and
noise_20k_all_ch.wav, expected in the directory given (by default
reference/measurements/nolting-sp12). Prints what it measures beside what the
model predicts, and the values for the measured SP-12 calibration.

Needs build/probe: make cli.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure as M  # noqa: E402

TABLE = [50, 53, 56, 59, 63, 67, 71, 75, 80, 84, 90, 95, 101, 107, 113, 120,
         127, 135, 143, 151, 160, 170, 180, 191, 202, 214, 227, 241, 255, 255, 255, 255]


def pitch(path):
    fs, x = M.load(path)
    m = x[:, 0]
    e = np.convolve(np.abs(m), np.ones(44) / 44, "same")
    idx = np.flatnonzero(e > 200)
    segs, s, p = [], idx[0], idx[0]
    for i in idx[1:]:
        if i - p > int(0.03 * fs):
            segs.append((s, p))
            s = i
        p = i
    segs.append((s, p))
    d = sorted({round((b - a) / fs, 3) for a, b in segs if (b - a) / fs > 0.5})
    # Repeats of one tuning agree to a few ms; merge them.
    merged = []
    for v in d:
        if merged and v - merged[-1][-1] < 0.004:
            merged[-1].append(v)
        else:
            merged.append([v])
    d = np.array([np.mean(g) for g in merged])
    steps = d[1:] / d[:-1]
    print(f"pitch: {len(d)} distinct note lengths; smallest step between neighbours {steps.min():.4f}")
    print("  the SP-1200 rule (byte + 1) / 128 never steps below "
          f"{min((TABLE[i + 1] + 1) / (TABLE[i] + 1) for i in range(28)):.4f}; "
          "the rule that drops the lowest bit has 48/46 = 1.0435")
    # Fit both rules to the mapping -8 .. +7 with +6 absent, longest first.
    idx = [8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 23]
    if len(d) == len(idx):
        for name, rule in (("(byte + 1) / 128", lambda v: (v + 1) / 128),
                           ("((byte | 1) + 1) / 128", lambda v: ((v | 1) + 1) / 128)):
            r = np.array([rule(TABLE[i]) for i in idx])
            k = d[::-1] * r
            print(f"  {name}: spread {1000 * np.std(k / k.mean()):.2f} per mille")


def noise(path):
    fs, x = M.load(path)
    m = x[:, 0]
    segs = M.bursts(m, fs)
    outs = [8, 7, 6, 5, 4, 3, 2, 1]
    P = {}
    for o, (a, b) in zip(outs, segs):
        f, P[o] = M.psd(m[a + int(0.6 * fs):b - int(0.05 * fs)], fs, 2048)
    print("fixed filters: frequency scale against the schematic, and the rms of what is left")
    scales = {}
    for o in (3, 4, 5, 6):
        sc, res = M.fixed_scale(f, 10 * np.log10(P[o] / P[8]), o)
        scales[o] = sc
        print(f"  output {o}: scale {sc:.3f}, residual {res:.2f} dB")

    f, P8 = M.psd(m[segs[0][0] + int(0.7 * fs):segs[0][1] - int(0.05 * fs)], fs, 4096)
    rest = {}
    for o, (a, b) in zip(outs, segs):
        if o > 2:
            continue
        f, Po = M.psd(m[a + int(0.7 * fs):b - int(0.05 * fs)], fs, 4096)
        fp, gain, res = M.rest_pole(f, 10 * np.log10(Po / P8))
        rest[o] = fp
        print(f"dynamic output {o} at rest: four matched poles at {fp:.0f} Hz, gain {gain:+.2f} dB, "
              f"residual {res:.2f} dB")

    on8 = M.onset(m, segs[0][0])
    fits = {}
    for o, (a, b) in zip(outs, segs):
        if o > 2:
            continue
        on = M.onset(m, a)
        meas = M.sweep(m[on:], m[on8:], fs)
        e, ohms, cap = M.fit_sweep(meas, "sp12", rest[o])
        fits[o] = (ohms, cap)
        pt, pf = M.probe("sp12", rest[o], ohms, cap)
        print(f"dynamic output {o} sweep: best pin 13 load {ohms:.0f} ohms, C111 x {cap:.3f}, "
              f"rms {e:.3f} octaves")
        for t, fp, _ in meas[[6, 18, 36, 54, 72]]:
            print(f"    {1000 * t:5.0f} ms: measured {fp:7.0f} Hz, model {np.interp(t, pt, pf):7.0f} Hz")
    return scales, rest, fits


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(M.ROOT, "reference", "measurements", "nolting-sp12")
    print("validate_sp12: Nolting's SP-12 recordings in", d)
    pitch(os.path.join(d, "sine_sweep_all_ch_tune.wav"))
    scales, rest, fits = noise(os.path.join(d, "noise_20k_all_ch.wav"))
    print("measured SP-12 calibration:")
    print("  fixed_scale", " ".join(f"{scales[o]:.3f}" for o in (3, 4, 5, 6)))
    print("  dyn_rest_hz", f"{rest[1]:.0f} {rest[2]:.0f}")
    print("  dyn_pin13_ohms", f"{np.mean([fits[o][0] for o in (1, 2)]):.0f}",
          " dyn_cap_scale", f"{np.mean([fits[o][1] for o in (1, 2)]):.3f}")


if __name__ == "__main__":
    main()
