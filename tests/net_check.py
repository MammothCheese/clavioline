#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the chip's network (tests/net_test.c) with the reference model (tools/clav_net_ref.py), bilinear
warping included: the response to white noise in dB at 200 log-spaced points, 50 Hz..12 kHz, where the reference is within 40 dB
of its own peak (deep notches are skipped: there any tiny shift is a large dB error and inaudible). A point
also passes when the difference itself is 70 dB under the loudest response (the integer noise floor at the
quietest settings).
   tests/net_check.py BUILD/net_test"""
import os, subprocess, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import clav_net_ref as net

exe = str(Path(sys.argv[1]).resolve())
import os
HALF = os.environ.get("NET_HALF") is not None     # the light mode: the network at 22.05 kHz
if HALF:
    net.FS = net.FS / 2; net.T = 1.0 / net.FS
NS = 1 << 17
IMP = 65536000
SETS = [(), ("1", "O"), ("1", "V"), ("4",), ("3",), ("2", "3"), ("3", "7"), ("6",), ("1", "4", "8"), ("3", "4", "5"),
        ("1", "4", "O"), ("1", "4", "6"), ("3", "4", "B"), ("3", "6", "8"), ("3", "B"), ("A",), ("A", "5"), ("B", "6"),
        ("1", "2", "3", "4", "5", "6", "7", "8", "9", "O", "A", "V"), ("9", "2", "A", "6", "V")]
LEVERS = [64, 40, 16, 4]
f = np.geomspace(50, 9000 if os.environ.get('NET_HALF') else 12000, 200)
worst = 0.0
TOP = 20 * np.log10(np.abs(net.ac(0, 64, f)).max())   # the loudest: no tablets, lever open
ok = True
for t in SETS:
    cfg = net.cfg_of(*t)
    for lv in (LEVERS if t in ((), ("1", "O"), ("3", "4", "5")) else [64]):
        raw = subprocess.run([exe, f"{cfg:x}", str(lv), str(NS), str(IMP)], capture_output=True, check=True).stdout
        xy = np.frombuffer(raw, dtype=np.int32).astype(float).reshape(-1, 2)
        x, y = xy[4096:, 0], xy[4096:, 1]          # (the start-up transient skipped)
        L = 16384
        w = np.hanning(L)
        sxx = syx = 0
        for k0 in range(0, len(x) - L, L // 2):   # averaged cross spectrum: H = Syx / Sxx
            X = np.fft.rfft(w * x[k0:k0 + L]); Y = np.fft.rfft(w * y[k0:k0 + L])
            sxx = sxx + np.abs(X) ** 2; syx = syx + Y * np.conj(X)
        H = syx / sxx * 1e6                        # ohm
        fd = np.fft.rfftfreq(L, 1 / net.FS)
        hd = 20 * np.log10(np.abs(np.interp(f, fd, np.abs(H))) + 1e-12)
        fw = np.tan(np.pi * f / net.FS) * net.FS / np.pi
        hr = 20 * np.log10(np.abs(net.ac(cfg, lv, fw)))
        m = hr > hr.max() - 40
        err = np.abs(hd - hr)[m].max()
        resid = 20 * np.log10(np.abs(10 ** (hd / 20) - 10 ** (hr / 20))[m].max() + 1e-12) - TOP   # dB re the loudest
        worst = max(worst, err)
        good = err < 0.5 or resid < -70          # (or the difference is 70 dB under the instrument's loudest)
        ok &= good
        print(f"{'PASS' if good else 'FAIL'}  {' '.join(t) or 'none':28s} lever {lv:2d}: max error {err:5.2f} dB,"
              f" residual {resid:6.1f} dB re loudest  (peak {hr.max():5.1f} dB ohm)")
print(f"worst {worst:.2f} dB:", "ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
