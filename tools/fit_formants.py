#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fit the tone network (tools/clav_net_ref.py) with a few resonant sections, for every tablet combination.

The FM-1 does not simulate the network any more: for each of the 8192 combinations of tablets 1-9, O, A, B, V this
script computes the exact response of the circuit model and fits it with NSEC second-order sections (vector fitting,
then poles and zeros paired). Each section is stored as (zero frequency, zero Q, pole frequency, pole Q) in log2
form (6 bytes); the firmware turns them into biquads (bilinear transform at 22.05 kHz) when a tablet changes.

    python3 tools/fit_formants.py            -> firmware/src/clav_formants.h, and the fit errors
    python3 tools/fit_formants.py --sample N -> fit N random combinations only and report (no output file)
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import clav_net_ref as net

FS = 22050.0                                    # the rate the firmware runs the formant filter at
K = 2 * FS                                      # bilinear transform
NSEC = 5                                        # second-order sections per combination
WS = 2 * np.pi * 1000.0                         # frequency scaling for the fit (conditioning)
LEVER = 64                                      # fitted with the knee lever open; the lever is a gain on the chip
F_FIT = np.geomspace(30, 60000, 260)            # analog frequencies the fit sees (covers the warped band)
F_CHK = np.geomspace(50, 9000, 160)             # digital frequencies the error is measured at
OUT = Path(__file__).resolve().parent.parent / "firmware" / "src" / "clav_formants.h"


def responses(cfg, f):
    """V_OUT / I (ohm) of the chip's netlist at analog frequencies f, all at once (batched solve)."""
    N = net.N
    s = 2j * np.pi * f
    Y = np.zeros((len(f), N, N), complex)
    idx = np.arange(N)
    Y[:, idx, idx] += net.P["LEAK"]

    def stamp(a, b, y):
        if a >= 0: Y[:, a, a] += y
        if b >= 0: Y[:, b, b] += y
        if a >= 0 and b >= 0:
            Y[:, a, b] -= y; Y[:, b, a] -= y

    for kind, a, b, v in net.elements(cfg, LEVER):
        stamp(a, b, 1.0 / v if kind == "R" else s * v)
    stamp(net.ND["OUT"], net.ND["N1"], 1.0 / (net.P["R_L5"] + s * net.P["L5"]))
    Lm, R = net.lmatrix(), net.rw()
    Zw = R[None] + s[:, None, None] * Lm[None]
    Yw = np.linalg.inv(Zw)
    for k, (pk, qk) in enumerate(net.WINDINGS):
        for l, (pl, ql) in enumerate(net.WINDINGS):
            for (r, sr) in ((pk, 1), (qk, -1)):
                for (c, sc) in ((pl, 1), (ql, -1)):
                    if r >= 0 and c >= 0:
                        Y[:, r, c] += sr * sc * Yw[:, k, l]
    J = np.zeros((len(f), N, 1), complex)
    J[:, net.ND["IN"], 0] = 1.0
    return np.linalg.solve(Y, J)[:, net.ND["OUT"], 0]


def vfit(s, H, n, iters=8, span=(1.5, 3.0), damp=50.0):
    """Vector fitting (Gustavsen): H(s) ~ d + sum r/(s - a). s scaled. Returns poles, residues, d."""
    w = np.abs(s.imag)
    a = []
    for wk in np.geomspace(w.min() * span[0], w.max() / span[1], n // 2):
        a += [complex(-wk / damp, wk), complex(-wk / damp, -wk)]
    a = np.array(a)
    weight = 1.0 / np.maximum(np.abs(H), 1e-3 * np.abs(H).max())   # fit in relative terms

    def basis(a):
        cols, i = [], 0
        while i < len(a):
            if abs(a[i].imag) < 1e-12:
                cols.append(1 / (s - a[i])); i += 1
            else:
                p1, p2 = 1 / (s - a[i]), 1 / (s - a[i].conjugate())
                cols.append(p1 + p2); cols.append(1j * p1 - 1j * p2); i += 2
        return np.array(cols).T

    for _ in range(iters):
        Phi = basis(a)
        A = np.hstack([Phi, np.ones((len(s), 1)), -H[:, None] * Phi]) * weight[:, None]
        b = H * weight
        Ar = np.vstack([A.real, A.imag]); br = np.concatenate([b.real, b.imag])
        x = np.linalg.lstsq(Ar, br, rcond=None)[0]
        ct = x[len(a) + 1:]
        # new poles: eigenvalues of (Areal - b ct^T)
        n_ = len(a)
        Am = np.zeros((n_, n_)); bv = np.zeros(n_)
        i = 0
        while i < n_:
            if abs(a[i].imag) < 1e-12:
                Am[i, i] = a[i].real; bv[i] = 1; i += 1
            else:
                Am[i, i] = a[i].real; Am[i, i + 1] = a[i].imag
                Am[i + 1, i] = -a[i].imag; Am[i + 1, i + 1] = a[i].real
                bv[i] = 2; i += 2
        z = np.linalg.eigvals(Am - np.outer(bv, ct))
        z = np.where(z.real > 0, -z.real + 1j * z.imag, z)           # keep it stable
        z = np.where(np.abs(z.real) < 1e-6, -1e-6 + 1j * z.imag, z)
        # order as conjugate pairs then reals
        cpx = [p for p in z if p.imag > 1e-9]
        rl = sorted([p.real for p in z if abs(p.imag) <= 1e-9])
        a = []
        for p in cpx: a += [p, p.conjugate()]
        a += [complex(r, 0) for r in rl]
        a = np.array(a)
    Phi = basis(a)
    A = np.hstack([Phi, np.ones((len(s), 1))]) * weight[:, None]
    x = np.linalg.lstsq(np.vstack([A.real, A.imag]), np.concatenate([(H * weight).real, (H * weight).imag]), rcond=None)[0]
    res, i = [], 0
    while i < len(a):
        if abs(a[i].imag) < 1e-12:
            res.append(x[i]); i += 1
        else:
            res += [complex(x[i], x[i + 1]), complex(x[i], -x[i + 1])]; i += 2
    return a, np.array(res), x[len(a)]


def to_sections(a, r, d):
    """partial fractions -> gain and NSEC sections (wz, qz, wp, qp), real (rad/s, unscaled)"""
    num = d * np.poly(a).astype(complex)
    for i in range(len(a)):
        num = num + np.concatenate([[0], r[i] * np.poly(np.delete(a, i))])
    num = num.real
    # leading coefficient may be ~0 (strictly proper): keep it, the big roots act as zeros at infinity
    lead = num[0] if abs(num[0]) > 1e-12 * np.abs(num).max() else 1e-12 * np.abs(num).max()
    num = num.copy(); num[0] = lead
    zeros = np.roots(num)
    zeros = np.where(zeros.real > 0, -zeros.real + 1j * zeros.imag, zeros)   # magnitude only: minimum phase
    big = 2.0 ** 30 / WS                        # zeros far above the band only say "no zero here": park them at
    zeros = np.where(np.abs(zeros) > big, zeros / np.abs(zeros) * big, zeros)   # 2^30 rad/s (the gain is refitted)
    gain = lead

    def pairs(roots):
        cpx = [p for p in roots if p.imag > 1e-9 * max(1, abs(p))]
        rl = sorted([p.real for p in roots if abs(p.imag) <= 1e-9 * max(1, abs(p))])
        out = []
        for p in cpx:
            w = abs(p); out.append((w, w / max(-2 * p.real, 1e-12)))
        for i in range(0, len(rl) - 1, 2):
            z1, z2 = -rl[i], -rl[i + 1]
            z1, z2 = max(z1, 1e-9), max(z2, 1e-9)
            w = np.sqrt(z1 * z2); out.append((w, w / (z1 + z2)))
        return sorted(out)

    zp, pp = pairs(zeros), pairs(a)
    secs = []
    for (wz, qz), (wp, qp) in zip(zp, pp):
        secs.append((wz * WS, qz, wp * WS, qp))
    # the s-scaling: H(s) = gain * prod(s'^2 + ...)/prod(...) with s' = s/WS -> same in rad/s with gain unchanged
    return gain, secs


def sec_response(gain, secs, f):
    s = 2j * np.pi * f
    h = np.full(len(f), gain, complex)
    for wz, qz, wp, qp in secs:
        h *= (s * s + s * wz / qz + wz * wz) / (s * s + s * wp / qp + wp * wp) * (WS * WS) ** 0
    return h


TRIES = [dict(span=(1.5, 3.0), damp=50.0, iters=8), dict(span=(1.0, 10.0), damp=10.0, iters=15),
         dict(span=(3.0, 30.0), damp=100.0, iters=15), dict(span=(1.0, 1.5), damp=5.0, iters=20),
         dict(span=(2.0, 100.0), damp=20.0, iters=25)]


def fit(cfg):
    """the best of a few starting points (vector fitting can settle badly); the error is measured on the digital
    response the firmware will produce (bilinear at 22.05 kHz) against the circuit model's"""
    H = responses(cfg, F_FIT)
    s = 2j * np.pi * F_FIT / WS
    fw = np.tan(np.pi * F_CHK / FS) * FS / np.pi
    href = np.abs(responses(cfg, fw))
    m = 20 * np.log10(href) > 20 * np.log10(href.max()) - 40
    best = None
    for opt in TRIES:
        try:
            a, r, d = vfit(s, H, 2 * NSEC, **opt)
            gain, secs = to_sections(a, r, d)
            if len(secs) != NSEC:
                continue
            hfit = np.abs(sec_response(1.0, secs, fw))
            g = np.exp(np.mean(np.log(href[m] / hfit[m])))
            err = np.abs(20 * np.log10(href[m] / (g * hfit[m]))).max()
        except (np.linalg.LinAlgError, ValueError, FloatingPointError):
            continue
        if best is None or err < best[2]:
            best = (g, secs, err)
        if err < 0.5:
            break
    return best


if __name__ == "__main__":
    import random
    if len(sys.argv) > 2 and sys.argv[1] == "--sample":
        random.seed(1)
        errs = []
        for _ in range(int(sys.argv[2])):
            cfg = random.randrange(1 << 13)
            errs.append(fit(cfg)[2])
        errs = np.array(errs)
        print(f"NSEC {NSEC}: median {np.median(errs):.2f} dB, 90% {np.percentile(errs, 90):.2f}, worst {errs.max():.2f}")
        sys.exit(0)

    # ---- every combination -> firmware/src/clav_formants.h
    fw = np.tan(np.pi * F_CHK / FS) * FS / np.pi
    hpk = np.abs(responses(0, fw)).max()        # no tablets, lever open: its peak plays at 14000 (Q15) from full scale
    out_scale = 14000.0 / 32768.0 / hpk

    def enc_w(w):                               # frequency, rad/s: log2(w) * 1600 in 16 bits (1/1600 octave)
        return int(min(65535, max(0, round(np.log2(max(w, 1.0)) * 1600.0))))

    def enc_q(q):                               # Q: (log2(q) + 16) * 8 in 8 bits (1/8 octave)
        return int(min(255, max(0, round((np.log2(max(q, 1e-12)) + 16.0) * 8.0))))

    def dec(row):
        g = 2.0 ** ((row[0] | row[1] << 8) / 256.0 - 128.0)
        secs = []
        for k in range(NSEC):
            b = row[2 + 6 * k: 8 + 6 * k]
            secs.append((2 ** ((b[0] | b[1] << 8) / 1600.0), 2 ** (b[2] / 8.0 - 16.0),
                         2 ** ((b[3] | b[4] << 8) / 1600.0), 2 ** (b[5] / 8.0 - 16.0)))
        return g, secs

    rows, errs = [], []
    for cfg in range(1 << 13):
        g, secs, _ = fit(cfg)
        gl = int(min(65535, max(0, round((np.log2(g * out_scale) + 128.0) * 256.0))))
        row = [gl & 255, gl >> 8]
        for wz, qz, wp, qp in secs:
            a, b = enc_w(wz), enc_w(wp)
            row += [a & 255, a >> 8, enc_q(qz), b & 255, b >> 8, enc_q(qp)]
        # the gain refitted on the rounded sections, then the error of what the firmware gets
        gq, sq = dec(row)
        href = np.abs(responses(cfg, fw))
        m = 20 * np.log10(href) > 20 * np.log10(href.max()) - 40
        hs = np.abs(sec_response(1.0, sq, fw))
        g2 = np.exp(np.mean(np.log(href[m] / hs[m])))
        gl = int(min(65535, max(0, round((np.log2(g2 * out_scale) + 128.0) * 256.0))))
        row[0], row[1] = gl & 255, gl >> 8
        gq, sq = dec(row)
        hq = gq * hs / out_scale
        errs.append(np.abs(20 * np.log10(href[m] / hq[m])).max())
        rows.append(row)
        if cfg % 1024 == 1023:
            print(f"{cfg + 1} fitted", flush=True)
    errs = np.array(errs)
    lines = ["/* Generated by tools/fit_formants.py: do not edit. SPDX-License-Identifier: GPL-3.0-only */",
             "#pragma once", "#include <stdint.h>",
             f"/* The tone network, fitted per tablet combination (bit 0 = tablet 1 .. bit 12 = V) with {NSEC} sections.",
             f" * Fit error against the circuit model, 50 Hz..9 kHz where within 40 dB of the peak: median "
             f"{np.median(errs):.2f} dB, 99% {np.percentile(errs, 99):.2f} dB, worst {errs.max():.2f} dB.",
             " * Row (bytes): gain as (log2(g) + 128) * 256 in 16 bits (Q15 out per Q15 in), then per section",
             " * wz (16 bits: log2(rad/s) * 1600), qz (8 bits: (log2 + 16) * 8), wp, qp the same.",
             " * Section: (s^2 + s wz/qz + wz^2) / (s^2 + s wp/qp + wp^2), bilinear at 22.05 kHz on the chip. */",
             f"#define FORMANT_NSEC {NSEC}",
             f"static const uint8_t FORMANT[{1 << 13}][{2 + 6 * NSEC}] = {{"]
    for r in rows:
        lines.append("    {" + ",".join(str(v) for v in r) + "},")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT}: median {np.median(errs):.2f} dB, 99% {np.percentile(errs, 99):.2f} dB, worst {errs.max():.2f} dB")
