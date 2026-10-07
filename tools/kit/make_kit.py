#!/usr/bin/env python3
"""Writes the test kit's two sounds into a directory (default the current
one): kit_noise.wav, 2.4 s of white noise, and kit_tone.wav, 1.5 s of
1 kHz, both 48 kHz 24-bit mono. analyse.py says what to do with them."""
import os
import sys

import numpy as np
import scipy.io.wavfile as wf

FS = 48000


def main(out):
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(1200)
    noise = rng.standard_normal(int(2.4 * FS)) * 10 ** (-14 / 20)
    noise = np.clip(noise, -0.9, 0.9)
    t = np.arange(int(1.5 * FS)) / FS
    tone = 0.7 * np.sin(2 * np.pi * 1000.0 * t)
    for name, x in (("kit_noise.wav", noise), ("kit_tone.wav", tone)):
        wf.write(os.path.join(out, name), FS, (x * (2 ** 31 - 1)).astype(np.int32))
    print(f"make_kit: wrote kit_noise.wav and kit_tone.wav in {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
