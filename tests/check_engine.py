#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host checks of the whole instrument (tests/run_engine.sh runs them): pitch, priority, legato, attack and P, the
coupler, the knee lever, aliasing, vibrato rates and depths, the Concert sub-octave. Prints PASS/FAIL lines.
   check_engine.py core CORE.wav [ALIAS.wav]      check_engine.py stage4 STAGE4.wav"""
import sys, wave
import numpy as np

FS = 44100
ok = True


def load(path):
    with wave.open(path) as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)[:, 0].astype(float)


def seg(x, t0, t1):
    return x[int(t0 * FS / 1000):int(t1 * FS / 1000)]


def f0_acf(s, fmin=60, fmax=2500):
    """the period by autocorrelation: the first lag whose correlation is within 10 % of the best"""
    s = s - s.mean()
    n = len(s)
    r = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * n)) ** 2)[:n]
    lo, hi = int(FS / fmax), int(FS / fmin)
    best = r[lo:hi].max()
    k = lo + int(np.argmax(r[lo:hi] > 0.9 * best))
    while k + 1 < hi and r[k + 1] > r[k]:
        k += 1
    a, b, c = r[k - 1], r[k], r[k + 1]
    return FS / (k + 0.5 * (a - c) / (a - 2 * b + c))


def env_db(x, win_ms=2.0):
    w = int(FS * win_ms / 1000)
    n = len(x) // w
    return 20 * np.log10(np.sqrt(np.mean(x[:n * w].reshape(n, w) ** 2, axis=1)) + 1e-9), w


def rms_db(s):
    return 20 * np.log10(np.sqrt(np.mean(s ** 2)) + 1e-9)


def check(name, cond, detail):
    global ok
    ok &= bool(cond)
    print(f"{'PASS' if cond else 'FAIL'}  {name}: {detail}")


def attack(x, t_key, ref_t0, ref_t1):
    e, w = env_db(x)
    ref = rms_db(seg(x, ref_t0, ref_t1))
    i0 = int(t_key * FS / 1000 / w)
    return {lvl: np.argmax(e[i0:] > ref + lvl) * w * 1000 / FS for lvl in (-40, -20, -3)}


def cents(f, f0):
    return 1200 * np.log2(f / f0)


def core(path, alias_path=None):
    x = load(path)
    for name, t0, t1, f0 in (("C4 alone", 400, 750, 261.63), ("C4 + F3: higher wins", 900, 1150, 261.63),
                             ("+ C5: jumps up", 1300, 1650, 523.25), ("C5 released: back to C4", 1800, 2050, 261.63),
                             ("coupler HIGH: C4 key -> C5", 4000, 4250, 523.25)):
        f = f0_acf(seg(x, t0, t1))
        check(name, abs(cents(f, f0)) < 3, f"{f:.2f} Hz (want {f0}, {cents(f, f0):+.1f} cents)")
    e, w = env_db(x)
    for t in (1200, 1700):
        i = int(t * FS / 1000 / w)
        d = e[i - 20:i + 40].max() - e[i - 20:i + 40].min()
        check(f"legato at {t} ms, no re-attack", d < 8,
              f"level range {d:.1f} dB around the change")
    a = attack(x, 200, 600, 750)
    check("attack, normal", 5 < a[-3] < 200, f"to -40/-20/-3 dB: {a[-40]:.1f} / {a[-20]:.1f} / {a[-3]:.1f} ms")
    b = attack(x, 2800, 3200, 3350)
    check("attack, tablet P is faster", b[-3] < a[-3] / 4, f"to -40/-20/-3 dB: {b[-40]:.1f} / {b[-20]:.1f} / {b[-3]:.1f} ms")
    pk = np.abs(seg(x, 2800, 2840)).max() / np.abs(seg(x, 3200, 3350)).max()
    d = rms_db(seg(x, 3050, 3150)) - rms_db(seg(x, 3300, 3390))
    check("tablet P: the note still sustains", abs(d) < 2, f"{d:+.1f} dB from 0.25 s to 0.5 s")
    pk_n = np.abs(seg(x, 200, 240)).max() / np.abs(seg(x, 600, 750)).max()
    pk_p = np.abs(seg(x, 2800, 2840)).max() / np.abs(seg(x, 3200, 3350)).max()
    d = 20 * np.log10(pk_p / pk_n)
    check("P peaks well over a note without P", d > 6,
          f"first 40 ms over the steady level: {20 * np.log10(pk_p):+.1f} dB with P, {20 * np.log10(pk_n):+.1f} dB without")
    d = rms_db(seg(x, 4100, 4250)) - rms_db(seg(x, 4500, 4650))
    check("knee lever 48 -> 8 steps", d > 15, f"{d:.1f} dB down")
    tail = seg(x, 2250, 2350)
    check("silence after release", np.abs(tail).max() < 50, f"peak {np.abs(tail).max():.0f}")
    if alias_path:
        y = seg(load(alias_path), 800, 2200)
        n = len(y)
        spec = np.abs(np.fft.rfft((y - y.mean()) * np.blackman(n))) ** 2
        f = np.fft.rfftfreq(n, 1 / FS)
        f0 = f0_acf(y, 1000, 5000)
        harm = np.zeros_like(f, dtype=bool)
        for k in range(1, int(20000 / f0) + 1):
            harm |= np.abs(f - k * f0) < 25
        band = (f > 50) & (f < 20000)
        a_db = 10 * np.log10(spec[band & ~harm].sum() / spec[band & harm].sum())
        check("alias energy, G7 (3136 Hz)", a_db < -38, f"{a_db:.1f} dB below the harmonics")


def pitch_track(s, hop=441, win=2048):
    return np.array([f0_acf(s[i:i + win], 150, 400) for i in range(0, len(s) - win, hop)]), FS / hop


def vib(x, t0, t1):
    tr, fr = pitch_track(seg(x, t0, t1))
    c = cents(tr, np.median(tr))
    c = c - c.mean()
    spec = np.abs(np.fft.rfft(c * np.hanning(len(c)), 8 * len(c)))
    f = np.fft.rfftfreq(8 * len(c), 1 / fr)
    band = (f > 2) & (f < 12)
    rate = f[band][np.argmax(spec[band])]
    depth = (np.percentile(c, 98) - np.percentile(c, 2)) / 2
    return rate, depth


def stage4(path):
    x = load(path)
    depth = []
    for name, t0, t1, want in (("vibrato I", 600, 2700, 4.5), ("vibrato II", 3300, 5400, 5.5),
                               ("vibrato III", 6000, 8100, 6.5)):
        r, d = vib(x, t0, t1)
        depth.append(d)
        check(name, abs(r - want) < 0.3, f"{r:.2f} Hz (want {want}), depth +-{d:.1f} cents")
    r, d = vib(x, 8700, 10800)
    check("vibrato III + AMP: deeper", 3.0 < d / depth[2] < 4.6,
          f"+-{d:.1f} cents, {d / depth[2]:.2f}x the shallow one (want ~3.8)")
    f = f0_acf(seg(x, 12000, 12500), 150, 400)
    check("STOPS mode: SEL plays D4", abs(cents(f, 293.66)) < 3, f"{f:.2f} Hz (want 293.66)")
    s = seg(x, 13400, 14500)
    n = len(s)
    spec = np.abs(np.fft.rfft((s - s.mean()) * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1 / FS)

    def lvl(fr):
        return 20 * np.log10(spec[(f > fr * 0.97) & (f < fr * 1.03)].max() + 1e-9)
    sub = lvl(130.81) - lvl(261.63)
    check("Sub I f: the octave below sounds", sub > -40, f"130.8 Hz at {sub:.1f} dB relative to 261.6 Hz")


def options(path):
    x = load(path)
    for name, t, want_decay in (("pluck decay 1.2 s with P", 700, True), ("decay on all notes, P off", 3600, True),
                                ("decay off: the note sustains", 6300, False)):
        d = rms_db(seg(x, t + 150, t + 250)) - rms_db(seg(x, t + 1250, t + 1350))
        ok_ = d > 25 if want_decay else abs(d) < 3
        check(name, ok_, f"{d:.1f} dB down between 0.2 s and 1.3 s")


def pluck(path):
    """runs of P notes (tests/scripts/pluck.txt), against a Clavioline recording's P demonstration: there each note
    falls 7.6 dB by 20 ms and 10.0 dB by 40 ms after its peak (the mean of 14 notes)"""
    x = load(path)
    e, w = env_db(x, 2.0)
    curves = []
    for k in range(16):
        i0 = int((0.4 + k * 0.3) * 1000 / 2.0)
        sg = e[i0:i0 + 125]
        ip = int(np.argmax(sg[:15]))
        curves.append(sg[ip:ip + 100] - sg[ip])
    c = np.mean(curves, axis=0)
    d20, d40, late = c[10], c[20], c[40:95]
    check("P notes: the fall after the peak", -11 < d20 < -5 and -13 < d40 < -7,
          f"{d20:+.1f} dB at 20 ms, {d40:+.1f} dB at 40 ms (recording: -7.6, -10.0)")
    check("P notes: then they sustain", late.max() - late.min() < 4, f"80..190 ms within {late.max() - late.min():.1f} dB")
    first = [cv[20] for cv in curves]
    check("P notes: every note in a run re-peaks", max(first) < -7, f"40 ms after each peak: {min(first):.1f} .. {max(first):.1f} dB")


def arp(path):
    """the arpeggiator (tests/scripts/arp.txt): the held notes in turn, a sixteenth note (125 ms) each, re-attacked"""
    x = load(path)
    C4, E4, G4, C5, E5 = 261.63, 329.63, 392.0, 523.25, 659.26

    def steps(t0, n):
        return [f0_acf(seg(x, t0 + k * 125 + 25, t0 + k * 125 + 70)) for k in range(n)]

    def names(fs):
        return " ".join(f"{f:.0f}" for f in fs)

    def match(fs, want):
        return all(abs(1200 * np.log2(f / w)) < 30 for f, w in zip(fs, want))

    got = steps(700, 9)
    check("UP: C E G C E G ...", match(got, [C4, E4, G4] * 3), names(got) + " Hz")
    got = steps(3600, 9)
    check("UP-DOWN, 2 octaves: C E C5 E5 C5 E C ...", match(got, [C4, E4, C5, E5, C5, E4, C4, E4, C5]), names(got) + " Hz")
    e, w = env_db(x, 2.0)
    gaps = [e[int((700 + k * 125 + 100) / 2):int((700 + k * 125 + 122) / 2)].min() - e[int((700 + k * 125 + 10) / 2):int((700 + k * 125 + 70) / 2)].max() for k in range(1, 8)]
    check("every step re-attacks (a gap before the next)", max(gaps) < -20, f"the gaps {max(gaps):.0f} dB or lower")
    f = f0_acf(seg(x, 6600, 7200))
    check("OFF: the highest held note", abs(1200 * np.log2(f / E4)) < 30, f"{f:.1f} Hz")


def glide(path):
    """portamento and the knee lever speed (tests/scripts/glide.txt)"""
    x = load(path)
    C4, C5 = 261.63, 523.25
    f = f0_acf(seg(x, 720, 780))
    check("portamento off: the new note at once", abs(1200 * np.log2(f / C5)) < 30, f"{f:.1f} Hz 20..80 ms after")
    f1 = f0_acf(seg(x, 2240, 2290))
    f2 = f0_acf(seg(x, 2750, 2850))
    check("portamento 120 ms: on the way, then there", C4 * 1.15 < f1 < C5 * 0.9 and abs(1200 * np.log2(f2 / C5)) < 30,
          f"{f1:.1f} Hz 40..90 ms after, {f2:.1f} Hz at 0.55 s")
    fast = rms_db(seg(x, 3300, 3450)) - rms_db(seg(x, 3700, 3850))
    fine = rms_db(seg(x, 4800, 4950)) - rms_db(seg(x, 5200, 5350))
    check("knee speed: 10 detents move the lever further on VERY FAST than on FINE", fast > fine + 6,
          f"{fast:.1f} dB down against {fine:.1f} dB")


if __name__ == "__main__":
    if sys.argv[1] == "core":
        core(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    elif sys.argv[1] == "glide":
        glide(sys.argv[2])
    elif sys.argv[1] == "arp":
        arp(sys.argv[2])
    elif sys.argv[1] == "options":
        options(sys.argv[2])
    elif sys.argv[1] == "pluck":
        pluck(sys.argv[2])
    else:
        stage4(sys.argv[2])
    print("ALL PASS" if ok else "SOME FAILED")
    sys.exit(0 if ok else 1)
