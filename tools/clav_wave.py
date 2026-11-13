#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The oscillator's wave shape, as the control valve passes it on (all ESTIMATES: the article says only
"trapezoidal in form"):

  the 6SN7 multivibrator's anode drops sharply when its triode conducts (for DUTY of the period), then recovers
  exponentially as the timing capacitor recharges (time constant TAU of the period); the 6BA6 control valve inverts
  that and clips it softly and unevenly (CLIP, BIAS), which squares the top and keeps the sloped edge.

An uneven pulse like this has even harmonics as well as odd ones: reedy, where a 50 % square is hollow."""
import numpy as np

DUTY = 0.30        # ESTIMATE: the part of the period the multivibrator's output triode conducts
TAU = 0.10         # ESTIMATE: the recovery time constant, as a part of the period
CLIP = 2.2         # ESTIMATE: how hard the control valve clips
BIAS = 0.25        # ESTIMATE: its uneven bias (more squashing on one side)


def cycle(n=8192):
    t = np.arange(n) / n
    low = t < DUTY
    a = np.where(low, 0.0, 1.0 - np.exp(-(t - DUTY) / TAU))
    # the low segment starts where the recovery left off: the sharp drop happens at t = 0
    y = -(a - a.mean())                         # the control valve inverts
    y = y / np.abs(y).max()
    y = np.tanh(CLIP * (y + BIAS)) - np.tanh(CLIP * BIAS)
    y = y - y.mean()
    return y / np.abs(y).max()


def square(n=8192):
    t = np.arange(n) / n
    return np.where(t < 0.5, 1.0, -1.0)


def bandlimited(wave, nmax, n_out):
    """keep harmonics 1..nmax, with a gentle taper over the top quarter (no ringing), resampled to n_out points"""
    spec = np.fft.rfft(wave)
    spec[0] = 0
    k = np.arange(len(spec))
    taper = np.clip((nmax + 1 - k) / (0.25 * nmax + 1), 0, 1)
    spec = spec * taper
    spec[nmax + 1:] = 0
    full = np.fft.irfft(spec, len(wave))
    out = full[:: len(wave) // n_out]
    return out


if __name__ == "__main__":
    y = cycle()
    h = np.abs(np.fft.rfft(y))[1:13]
    print("harmonics 1..12 (dB):", " ".join(f"{v:4.0f}" for v in 20 * np.log10(h / h.max())))
    s = square()
    h = np.abs(np.fft.rfft(s))[1:13]
    print("square    1..12 (dB):", " ".join(f"{v:4.0f}" for v in 20 * np.log10(h / h.max() + 1e-9)))
