"""Measurements of a real machine's outputs, shared by validate_sp12.py and
the owner test kit. Everything works on ratios between two outputs that
played the same sound, so the source, the input stage and the recorder
cancel out."""
import csv
import os
import subprocess

import numpy as np
import scipy.io.wavfile as wf
import scipy.signal as ss
from scipy.optimize import minimize, minimize_scalar

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(path):
    fs, x = wf.read(path)
    x = x.astype(float)
    if x.ndim > 1:
        return fs, x
    return fs, x[:, None]


def bursts(x, fs, threshold=100.0, gap=0.1, shortest=0.5):
    """(start, end) sample pairs of the sounds in a recording, separated by
    at least gap seconds of quiet."""
    e = np.convolve(np.abs(x), np.ones(441) / 441, "same")
    idx = np.flatnonzero(e > threshold)
    out, s, p = [], idx[0], idx[0]
    for i in idx[1:]:
        if i - p > int(gap * fs):
            out.append((s, p))
            s = i
        p = i
    out.append((s, p))
    return [(a, b) for a, b in out if (b - a) / fs > shortest]


def onset(x, a, level=200.0):
    w = x[max(a - 2000, 0):a + 2000]
    return max(a - 2000, 0) + int(np.flatnonzero(np.abs(w) > level)[0])


def psd(x, fs, n=4096):
    return ss.welch(x, fs, nperseg=n)


def spice_curve(name):
    rows = list(csv.DictReader(open(os.path.join(ROOT, "tests", "data", f"spice_{name}.csv"))))
    f = np.array([float(r["freq_hz"]) for r in rows])
    m = np.array([float(r["mag_db"]) for r in rows])
    return f, m - m[0]


def fixed_scale(f, ratio_db, output):
    """The frequency scale that best lays the schematic's fixed filter for
    output 3 to 6 over a measured response, and the rms residual in dB."""
    fr, mg = spice_curve(f"fixed{output}")

    def model(sc):
        return np.interp(f / sc, fr, mg)

    keep = (f > 1500) & (f < 14000) & (model(1.0) > -30)
    r = minimize_scalar(lambda sc: np.mean((ratio_db[keep] - model(sc)[keep]) ** 2),
                        bounds=(0.8, 1.25), method="bounded")
    return r.x, float(np.sqrt(r.fun))


def four_pole_db(f, fp):
    """Four matched poles at fp and the output stage's pole at 22.6 kHz."""
    return -40 * np.log10(1 + (f / fp) ** 2) - 10 * np.log10(1 + (f / 22575.0) ** 2)


def rest_pole(f, ratio_db, top=5000.0):
    """The resting pole of a dynamic output from its response against a raw
    one, fitted over the band clear of the recording's noise floor."""
    band = (f > 90) & (f < top)
    best = None
    for lf in np.linspace(np.log10(300), np.log10(5000), 40):
        r = minimize(lambda v: np.mean((ratio_db[band] - v[1] - four_pole_db(f[band], 10 ** v[0])) ** 2),
                     [lf, 0.0], method="Nelder-Mead")
        if best is None or r.fun < best.fun:
            best = r
    return 10 ** best.x[0], best.x[1], float(np.sqrt(best.fun))


def sweep(x_dyn, x_raw, fs, frames=90, n=1024, hop=220):
    """The pole of a dynamic output, frame by frame from its onset, against a
    raw output that played the same sound: (time, pole Hz, fit rms dB)."""
    win = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / fs)
    band = (f > 300) & (f < 14000)

    def smooth(p):
        return np.array([p[max(1, int(i / 1.06)):int(i * 1.06) + 2].mean() for i in range(len(p))])

    out = []
    for k in range(frames):
        t0 = k * hop
        a = np.abs(np.fft.rfft(x_raw[t0:t0 + n] * win)) ** 2
        b = np.abs(np.fft.rfft(x_dyn[t0:t0 + n] * win)) ** 2
        ratio = 10 * np.log10(smooth(b) / smooth(a) + 1e-12)

        def cost(lf):
            mdl = four_pole_db(f, 10 ** lf)
            keep = band & (mdl > -35)
            if keep.sum() < 6:
                return 1e9
            g = np.mean(ratio[keep] - mdl[keep])
            return np.mean((ratio[keep] - mdl[keep] - g) ** 2)

        r = minimize_scalar(cost, bounds=(np.log10(300), np.log10(80000)), method="bounded")
        out.append(((t0 + n / 2) / fs, 10 ** r.x, float(np.sqrt(r.fun))))
    return np.array(out)


def probe(machine, rest_hz, pin13_ohms, cap_scale):
    """The library's own prediction of the pole after a note."""
    exe = os.path.join(ROOT, "build", "probe")
    txt = subprocess.run([exe, machine, str(rest_hz), str(pin13_ohms), str(cap_scale)],
                         capture_output=True, text=True, check=True).stdout
    a = np.array([[float(v) for v in line.split()] for line in txt.splitlines()])
    return a[:, 0], a[:, 1]


def fit_sweep(measured, machine, rest_hz, t_lo=0.03, t_hi=0.3):
    """The pin 13 load and C111 scale whose predicted sweep best matches a
    measured one, judged in octaves between t_lo and t_hi."""
    t, fp = measured[:, 0], measured[:, 1]
    keep = (t >= t_lo) & (t <= t_hi)

    def err(ohms, cap):
        pt, pf = probe(machine, rest_hz, ohms, cap)
        model = np.interp(t[keep], pt, pf)
        return float(np.sqrt(np.mean(np.log2(fp[keep] / model) ** 2)))

    best = (1e9, 0.0, 1.0)
    for ohms in (0, 500, 700, 850, 1000, 1200, 1500, 2000, 3000, 5000):
        for cap in (0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.2):
            e = err(ohms, cap)
            if e < best[0]:
                best = (e, ohms, cap)
    # Refine around the best grid point.
    e0, o0, c0 = best
    for ohms in np.linspace(max(o0 * 0.7, 1) if o0 else 0, (o0 or 300) * 1.3, 9):
        for cap in np.linspace(c0 - 0.04, c0 + 0.04, 9):
            e = err(ohms, cap)
            if e < best[0]:
                best = (e, ohms, cap)
    return best
