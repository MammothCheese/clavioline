#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the chip's formant filter (tests/formant_test.c) with the circuit model it was fitted to, end to end
(fit, table rounding, decoding, fixed point): the response to white noise in dB, 50 Hz..9 kHz, where the model is
within 40 dB of its peak. Over the Selmer voicings and random combinations.   formant_check.py BUILD/formant_test"""
import random, subprocess, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import clav_net_ref as net

FS = 22050.0
exe = str(Path(sys.argv[1]).resolve())
f = np.geomspace(50, 9000, 160)
fw = np.tan(np.pi * f / FS) * FS / np.pi
hpk = np.abs(net.ac(0, 64, np.tan(np.pi * np.geomspace(50, 9000, 160) / FS) * FS / np.pi)).max()
scale = 14000.0 / 32768.0 / hpk                 # as tools/fit_formants.py: Q15 out per Q15 in
SETS = [(), ("1", "O"), ("1", "V"), ("4",), ("3",), ("2", "3"), ("3", "7"), ("6",), ("1", "4", "8"), ("3", "4", "5"),
        ("1", "4", "O"), ("1", "4", "6"), ("3", "4", "B"), ("3", "6", "8"), ("3", "B"), ("4", "6"), ("4", "9"),
        ("3", "5", "6", "8"), ("B", "O"), ("B",), ("4", "5", "6"), ("3", "4", "6"), ("1", "9")]
random.seed(11)
cfgs = [net.cfg_of(*t) for t in SETS] + [random.randrange(1 << 13) for _ in range(25)]
NS, L = 1 << 16, 8192
worst, ok = 0.0, True
for cfg in cfgs:
    raw = subprocess.run([exe, f"{cfg:x}", str(NS)], capture_output=True, check=True).stdout
    xy = np.frombuffer(raw, dtype=np.int32).astype(float).reshape(-1, 2)
    x, y = xy[4096:, 0], xy[4096:, 1]
    w = np.hanning(L)
    sxx = syx = 0
    for k0 in range(0, len(x) - L, L // 2):
        X = np.fft.rfft(w * x[k0:k0 + L]); Y = np.fft.rfft(w * y[k0:k0 + L])
        sxx = sxx + np.abs(X) ** 2; syx = syx + Y * np.conj(X)
    fd = np.fft.rfftfreq(L, 1 / FS)
    hd = 20 * np.log10(np.interp(f, fd, np.abs(syx / sxx)) + 1e-12)
    hr = 20 * np.log10(np.abs(net.ac(cfg, 64, fw)) * scale)
    m = hr > hr.max() - 40
    err = np.abs(hd - hr)[m].max()
    worst = max(worst, err)
    good = err < 3.0
    ok &= good
    names = [n for i, n in enumerate(net.TAB[:13]) if cfg >> i & 1]
    if not good or cfg in cfgs[:len(SETS)]:
        print(f"{'PASS' if good else 'FAIL'}  {' '.join(names) or 'none':28s} max error {err:5.2f} dB")
print(f"{len(cfgs)} combinations, worst {worst:.2f} dB:", "ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
