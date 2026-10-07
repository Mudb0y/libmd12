#!/usr/bin/env python3
"""Plays the test kit's recording procedure on the library itself, with a
calibration made deliberately unlike the factory's, and checks that
analyse.py recovers it. The machine is rendered at 96 kHz, where the
library's filters are within 0.02 dB of the circuit's, so what is tested is
the analysis. Needs build/md12 and build/probe (make cli)."""
import os
import subprocess
import sys
import tempfile

import numpy as np
import scipy.io.wavfile as wf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MD12 = os.path.join(ROOT, "build", "md12")

TRUTH = {
    "pitch_lsb": [0],
    "fixed_scale": [1.05, 0.95, 1.08, 1.0],
    "dyn_rest_hz": [1150.0, 900.0],
    "dyn_pin13_ohms": [1100.0],
    "dyn_cap_scale": [0.93, 1.12],
    "dyn_gain": [0.8, 1.0],
}
TOLERANCE = {"pitch_lsb": 0, "fixed_scale": 0.01, "dyn_rest_hz": 0.03, "dyn_pin13_ohms": 0.15,
             "dyn_cap_scale": 0.05, "dyn_gain": 0.04}


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


def main():
    with tempfile.TemporaryDirectory() as d:
        run(sys.executable, os.path.join(HERE, "make_kit.py"), d)
        truth = os.path.join(d, "truth.cal")
        with open(truth, "w") as f:
            f.write("machine sp1200\n")
            for k, v in TRUTH.items():
                f.write(k + " " + " ".join(str(x) for x in v) + "\n")
        gap = np.zeros((int(0.8 * 96000), 2))
        takes = []
        for ch in range(1, 9):
            out = os.path.join(d, f"ch{ch}.wav")
            run(MD12, "sample", os.path.join(d, "kit_noise.wav"), out, "-c", str(ch), "-o", "tipring", "-k", truth,
                "-r", "96000")
            takes += [wf.read(out)[1], gap]
        wf.write(os.path.join(d, "noise.wav"), 96000, np.concatenate(takes).astype(np.float32))
        takes = []
        for t in range(-8, 8):
            out = os.path.join(d, f"t{t}.wav")
            run(MD12, "sample", os.path.join(d, "kit_tone.wav"), out, "-c", "8", "-t", str(t), "-k", truth,
                "-r", "96000")
            takes += [wf.read(out)[1], np.zeros(int(0.5 * 96000))]
        wf.write(os.path.join(d, "tones.wav"), 96000, np.concatenate(takes).astype(np.float32))
        got = os.path.join(d, "got.cal")
        subprocess.run([sys.executable, os.path.join(HERE, "analyse.py"), os.path.join(d, "noise.wav"),
                        os.path.join(d, "tones.wav"), "-o", got], check=True)
        found = {}
        for line in open(got):
            if line.startswith("#") or line.startswith("machine"):
                continue
            k, *v = line.split()
            found[k] = [float(x) for x in v]
    bad = 0
    for k, want in TRUTH.items():
        for w, g in zip(want, found[k]):
            ok = abs(g - w) <= TOLERANCE[k] * max(abs(w), 1)
            bad += not ok
            if not ok:
                print(f"selftest: {k} recovered {g}, set {w}")
    print(f"selftest: {'all recovered' if not bad else f'{bad} values missed'}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
