#!/usr/bin/env python3
"""Writes src/proto.c: the analogue filters as poles, zeros and gain.

Each comes from a netlist in tools/spice, which carries every part value with
its reference designator and schematic page, so the netlists stay the one
place the values live. A small nodal analysis with ideal op-amps (nullors)
gives the circuit as G + sC; its poles are the finite generalised eigenvalues
of that pencil, and its zeros those of the pencil bordered by the input and
output. The result is checked against ngspice's own sweep in tests/data
before anything is written.
"""
import csv
import os
import re
import sys

import numpy as np
import scipy.linalg as sl

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SUFFIX = {"t": 1e12, "g": 1e9, "meg": 1e6, "k": 1e3, "m": 1e-3, "u": 1e-6, "n": 1e-9, "p": 1e-12, "f": 1e-15}


def value(tok, params):
    tok = tok.strip()
    if tok.startswith("{"):
        expr = tok.strip("{}")
        expr = re.sub(r"(.+?)\?(.+?):(.+)", r"(\2 if \1 else \3)", expr)
        expr = re.sub(r"(\d+(?:\.\d+)?)(meg|[tgkmunpf])\b", lambda m: repr(float(m.group(1)) * SUFFIX[m.group(2).lower()]), expr, flags=re.I)
        return float(eval(expr, {}, dict(params)))
    m = re.match(r"^([-+]?\d*\.?\d+(?:e[-+]?\d+)?)(meg|[tgkmunpf])?", tok, re.I)
    if not m:
        raise ValueError(tok)
    v = float(m.group(1))
    if m.group(2):
        v *= SUFFIX[m.group(2).lower()]
    return v


def parse(path, overrides=None):
    params, elems = {}, []
    for line in open(path):
        line = line.split(";")[0].strip()
        if not line or line.startswith("*"):
            continue
        low = line.lower()
        if low.startswith(".param"):
            for k, v in re.findall(r"(\w+)\s*=\s*(\S+)", line[6:]):
                params[k] = value(v, params)
            continue
        if low.startswith(".control"):
            break
        if low.startswith(".") or low.startswith("e1"):
            continue
        elems.append(re.findall(r"\{[^}]*\}|\S+", line))
    params.update(overrides or {})
    return params, elems


def pencil(path, out, overrides=None):
    """G, C and the input vector b for V(out) / V(in), nodes and unknowns."""
    params, elems = parse(path, overrides)
    nodes = {}

    def n(name):
        if name == "0":
            return None
        return nodes.setdefault(name, len(nodes))

    for e in elems:
        for t in e[1:4] if e[0][0].upper() == "X" else e[1:3]:
            n(t)
    vsrc = [e for e in elems if e[0][0].upper() == "V"]
    amps = [e for e in elems if e[0][0].upper() == "X"]
    size = len(nodes) + len(vsrc) + len(amps)
    G = np.zeros((size, size))
    C = np.zeros((size, size))
    b = np.zeros(size)

    def stamp(M, a, c, y):
        if a is not None:
            M[a, a] += y
        if c is not None:
            M[c, c] += y
        if a is not None and c is not None:
            M[a, c] -= y
            M[c, a] -= y

    for e in elems:
        kind = e[0][0].upper()
        if kind == "R":
            stamp(G, n(e[1]), n(e[2]), 1.0 / value(e[3], params))
        elif kind == "C":
            stamp(C, n(e[1]), n(e[2]), value(e[3], params))
        elif kind == "I":
            # Flows from the first node through the source into the second.
            toks = [t.lower() for t in e]
            if "ac" in toks:
                i = value(e[toks.index("ac") + 1], params)
                if n(e[1]) is not None:
                    b[n(e[1])] -= i
                if n(e[2]) is not None:
                    b[n(e[2])] += i
    row = len(nodes)
    for e in vsrc:
        a, c = n(e[1]), n(e[2])
        if a is not None:
            G[a, row] += 1
            G[row, a] += 1
        if c is not None:
            G[c, row] -= 1
            G[row, c] -= 1
        toks = [t.lower() for t in e]
        if "ac" in toks:
            b[row] = value(e[toks.index("ac") + 1], params)
        row += 1
    for e in amps:
        # Ideal op-amp: its inputs are equal, and its output supplies any
        # current.
        inp, inn, o = n(e[1]), n(e[2]), n(e[3])
        if inp is not None:
            G[row, inp] += 1
        if inn is not None:
            G[row, inn] -= 1
        G[o, row] += 1
        row += 1
    return G, C, b, nodes[out]


def roots(G, C, b, o):
    w = sl.eig(G, -C, right=False)
    poles = w[np.isfinite(w)]
    size = G.shape[0]
    Gz = np.zeros((size + 1, size + 1))
    Cz = np.zeros((size + 1, size + 1))
    Gz[:size, :size], Cz[:size, :size] = G, C
    Gz[:size, size] = -b
    Gz[size, o] = 1
    w = sl.eig(Gz, -Cz, right=False)
    zeros = w[np.isfinite(w)]
    return poles, zeros


def response(G, C, b, o, f):
    return np.array([np.linalg.solve(G + 2j * np.pi * fk * C, b)[o] for fk in f])


def tidy(r, scale):
    """Drops roots far beyond audio (artefacts of the pencil) and pairs the
    rest: one entry per conjugate pair, imaginary part positive."""
    r = r[np.abs(r) < 2 * np.pi * 1e7]
    out = []
    for x in r:
        if abs(x.imag) <= 1e-9 * max(abs(x), scale):
            out.append(complex(x.real, 0.0))
        elif x.imag > 0:
            out.append(x)
    return sorted(out, key=lambda x: (abs(x), x.imag))


def proto(path, out, ref_hz=0.0, overrides=None, extra_poles=()):
    G, C, b, o = pencil(path, out, overrides)
    p, z = roots(G, C, b, o)
    p = tidy(p, 1.0)
    z = tidy(z, 1.0)
    p += [complex(-2 * np.pi * f, 0.0) for f in extra_poles]

    def h(f):
        s = 2j * np.pi * f
        v = 1.0 + 0j
        for x in z:
            v *= (s - x) * ((s - np.conj(x)) if x.imag > 0 else 1)
        for x in p:
            v /= (s - x) * ((s - np.conj(x)) if x.imag > 0 else 1)
        return v

    # Check the roots against the circuit itself before using them.
    f = np.logspace(1, 5, 200)
    hc = response(G, C, b, o, f)
    for x in extra_poles:
        hc = hc / (1 + 1j * f / x)
    hr = np.array([h(fk) for fk in f])
    k = hc[0] / hr[0]
    err = np.abs(20 * np.log10(np.abs(k * hr) / np.abs(hc)))
    keep = 20 * np.log10(np.abs(hc) / np.abs(hc).max()) > -80
    if err[keep].max() > 1e-3:
        raise SystemExit(f"{path}: roots disagree with the circuit by {err[keep].max():.4f} dB")
    # Normalise to unity gain at DC, or at ref_hz for a circuit that blocks DC.
    gain = 1.0 / abs(h(ref_hz))
    if np.real(k * h(ref_hz)) < 0:
        gain = -gain
    return p, z, gain, (lambda fk: gain * h(fk))


def check_spice(name, hfun, norm_hz=None):
    path = os.path.join(ROOT, "tests", "data", f"spice_{name}.csv")
    rows = list(csv.DictReader(open(path)))
    f = np.array([float(r["freq_hz"]) for r in rows])
    m = np.array([float(r["mag_db"]) for r in rows])
    ref = m[0] if norm_hz is None else np.interp(norm_hz, f, m)
    ours = np.array([20 * np.log10(abs(hfun(x))) for x in f])
    keep = (m - ref) > -80
    err = np.abs((ours - ours[0] if norm_hz is None else ours) - (m - ref))[keep]
    if err.max() > 0.01:
        raise SystemExit(f"{name}: {err.max():.4f} dB from ngspice")
    return err.max()


def c_proto(name, p, z, gain, comment):
    def roots_c(r):
        items = [f"{{{x.real:.10e}, {x.imag:.10e}}}" for x in r]
        return "{" + ", ".join(items) + "}" if items else "{{0.0, 0.0}}"
    return (f"/* {comment} */\n"
            f"const md12_proto {name} = {{\n"
            f"    {len(p)}, {len(z)},\n"
            f"    {roots_c(p)},\n"
            f"    {roots_c(z)},\n"
            f"    {gain:.10e},\n}};\n")


def main(out_path):
    spice = os.path.join(HERE, "spice")
    parts = []

    # The preamp at 0 dB (and 20 dB with both 4053 bits high): one pole near
    # 31 kHz from its feedback capacitors, ahead of the anti-alias filter.
    pp, pz, _, phf = proto(os.path.join(spice, "preamp.cir"), "out", overrides={"ga": 0, "gb": 1})
    check_spice("preamp_0db", phf)
    ap, az, _, ahf = proto(os.path.join(spice, "aa.cir"), "out")
    check_spice("aa", ahf)
    ip, iz, ig, _ = proto(os.path.join(spice, "aa.cir"), "out",
                          extra_poles=[-x.real / (2 * np.pi) for x in pp])
    parts.append(c_proto("md12_proto_input", ip, iz, ig,
                         "Sample input: the preamp's pole and the anti-alias filter (aa.cir, "
                         "preamp.cir), unity gain at DC"))

    names = []
    for ch in (3, 4, 5, 6):
        fp, fz, fg, fhf = proto(os.path.join(spice, f"fixed{ch}.cir"), "ring")
        check_spice(f"fixed{ch}", fhf)
        names.append(c_proto(f"md12_proto_fixed{ch}", fp, fz, fg, f"Output {ch}'s fixed filter (fixed{ch}.cir)"))
    parts += names

    dp, dz, dg, dhf = proto(os.path.join(spice, "dynout.cir"), "ring", ref_hz=1000.0)
    check_spice("dynout", dhf, norm_hz=1000.0)
    # The stage inverts, but which way the SSM2044's output current flows is
    # not drawn; Rossum says every output but the mix keeps the input's
    # polarity, so the stage is taken as non-inverting overall.
    parts.append(c_proto("md12_proto_dynout", dp, dz, abs(dg),
                         "Outputs 1 and 2 after the SSM2044 (dynout.cir), unity gain at 1 kHz"))

    with open(out_path, "w") as f:
        f.write("/* Generated by tools/gen_filters.py from tools/spice. Do not edit. */\n")
        f.write('#include "internal.h"\n\n')
        f.write("\n".join(parts))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "src", "proto.c"))
