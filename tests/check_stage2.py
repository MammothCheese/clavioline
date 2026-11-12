#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage 2 checks on the host renders (tests/run_stage2.sh runs this). Prints measurements and PASS/FAIL."""
import sys, wave
import numpy as np

FS = 44100


def load(path):
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)[:, 0].astype(float)
    return x


def seg(x, t0, t1):
    return x[int(t0 * FS / 1000):int(t1 * FS / 1000)]


def pitch(s):
    s = s - s.mean()
    n = len(s)
    spec = np.abs(np.fft.rfft(s * np.hanning(n), 8 * n))
    f = np.fft.rfftfreq(8 * n, 1 / FS)
    k = np.argmax(spec[f > 40]) + np.searchsorted(f, 40, side="right")
    return f[k]


def env_db(x, win_ms=2.0):
    w = int(FS * win_ms / 1000)
    n = len(x) // w
    r = np.sqrt(np.mean(x[:n * w].reshape(n, w) ** 2, axis=1)) + 1e-9
    return 20 * np.log10(r), w


ok = True


def check(name, cond, detail):
    global ok
    ok &= bool(cond)
    print(f"{'PASS' if cond else 'FAIL'}  {name}: {detail}")


def attack(x, t_key, ref_t0, ref_t1):
    e, w = env_db(x)
    ref = 20 * np.log10(np.sqrt(np.mean(seg(x, ref_t0, ref_t1) ** 2)) + 1e-9)
    i0 = int(t_key * FS / 1000 / w)
    out = {}
    for lvl in (-40, -20, -3):
        j = np.argmax(e[i0:] > ref + lvl)
        out[lvl] = j * w * 1000 / FS
    return out


x = load(sys.argv[1])
cents = lambda f, f0: 1200 * np.log2(f / f0)
for name, t0, t1, f0 in (("C4 alone", 400, 750, 261.63), ("C4 + F3: higher wins", 900, 1150, 261.63),
                         ("+ C5: jumps up", 1300, 1650, 523.25), ("C5 released: back to C4", 1800, 2050, 261.63),
                         ("coupler HIGH: C4 key -> C5", 4000, 4250, 523.25)):
    f = pitch(seg(x, t0, t1))
    check(name, abs(cents(f, f0)) < 5, f"{f:.2f} Hz (want {f0}, {cents(f, f0):+.1f} cents)")

e, w = env_db(x)
for t in (1200, 1700):                          # legato: no gate dip at the note change
    i = int(t * FS / 1000 / w)
    d = e[i - 20:i + 40].max() - e[i - 20:i + 40].min()
    check(f"legato at {t} ms, no re-attack", d < 3, f"level range {d:.1f} dB around the change")

a = attack(x, 200, 600, 750)
check("attack, normal", 5 < a[-3] < 200, f"to -40/-20/-3 dB: {a[-40]:.1f} / {a[-20]:.1f} / {a[-3]:.1f} ms")
b = attack(x, 2800, 3200, 3350)
check("attack, tablet P is faster", b[-3] < a[-3] / 4, f"to -40/-20/-3 dB: {b[-40]:.1f} / {b[-20]:.1f} / {b[-3]:.1f} ms")

l_hi = 20 * np.log10(np.sqrt(np.mean(seg(x, 4100, 4250) ** 2)))
l_lo = 20 * np.log10(np.sqrt(np.mean(seg(x, 4500, 4650) ** 2)))
check("knee lever 48 -> 8 steps", 20 < l_hi - l_lo < 40, f"{l_hi - l_lo:.1f} dB down (table: 28.1 dB)")
tail = seg(x, 2250, 2350)
check("silence after release", np.abs(tail).max() < 50, f"peak {np.abs(tail).max():.0f}")

if len(sys.argv) > 2:                           # alias check: a 3136 Hz note
    y = seg(load(sys.argv[2]), 800, 2200)
    n = len(y)
    spec = np.abs(np.fft.rfft((y - y.mean()) * np.blackman(n)))
    f = np.fft.rfftfreq(n, 1 / FS)
    f0 = pitch(y)
    harm = np.zeros_like(f, dtype=bool)
    for k in range(1, int(20000 / f0) + 1):
        harm |= np.abs(f - k * f0) < 25
    band = (f > 50) & (f < 20000)
    p = spec ** 2
    alias_db = 10 * np.log10(p[band & ~harm].sum() / p[band & harm].sum())
    check("alias energy, G7 trapezoid", alias_db < -40, f"{alias_db:.1f} dB below the harmonics (f0 {f0:.1f} Hz)")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
