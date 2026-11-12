#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The tone-forming network (Electronic Engineering, Oct. 1952, Fig. 4) as a reference model.

One netlist, two uses:
  * ac(config, lever, f)      the exact small-signal response V_OUT / I_V2 (complex nodal analysis)
  * companion constants        what firmware/src/clav_net.h runs on (trapezoidal companion models), written into
                               clav_tables.h by tools/gen_clav_tables.py

Tags: PRINTED (Fig. 4 / Fig. 2, 1952), CONCERT (Selmer Concert tone generator drawing DRG 1289, legible values),
ESTIMATE (not documented anywhere we have; chosen to put the resonances in the audio band - change freely).
"""
import numpy as np

FS = 44100.0
T = 1.0 / FS

# ---- tablets: bit numbers in a config word -------------------------------------------------------------
TAB = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "O", "A", "B", "V", "P",
       "VIB1", "VIB2", "VIB3", "AMP", "SUB1P", "SUB2P", "SUB1F", "SUB2F"]
BIT = {n: i for i, n in enumerate(TAB)}
NET_MASK = (1 << 13) - 1                         # tablets 1..9, O, A, B, V shape the network

# ---- nodes ---------------------------------------------------------------------------------------------
# A = V2 anode; ground = -1. N1 is the row C18 .. C21: the end of L1 and the foot of L5 (see the analysis, "the row":
# the drawing shows a small gap between C19 and R26; read as one node, the coils carry the signal to the amplifier,
# as in Martin's patent, Selmer's presets come out at similar levels, and tablet 6 alone (Cornet) does something)
NODES = ["A", "IN", "OUT", "NB", "N1", "N3", "a", "NV", "b"]
N = len(NODES)
ND = {n: i for i, n in enumerate(NODES)}
GND = -1

P = dict(
    R5=100e3,      # CONCERT: V2 anode load 100k
    C5=0.02e-6,    # CONCERT: V2 coupling capacitor .02
    C30=100e-12, R29=5e3, R30=3e6, C28=0.001e-6, R31=20e3, C29=0.05e-6,                    # PRINTED
    C17=0.01e-6,   # CONCERT, uncertain: the V capacitor is faint on the Concert drawing (".1" or ".01"). The 1952 label
                   # beside C17 reads "100pF", but then V changes nothing audible. Selmer offers "O or V" on the Violin
                   # card: with 0.01 uF, V darkens the tone at O's level (0.1 uF also drops it 6-10 dB)
    C18=0.02e-6, C19=0.005e-6, R26=250e3, C20=0.005e-6, R27=250e3, C21=0.015e-6,          # PRINTED
    C22=0.01e-6, C23=0.005e-6, C24=200e-12, C25=0.005e-6, C26=0.015e-6, R28=10e3,         # PRINTED
    C27=0.02e-6,                                                                          # PRINTED
    R_SHORT=100.0,  # a closed B or V contact, as 100 ohm (keeps the matrix well scaled; << every impedance there)
    RIN=1e6,        # ESTIMATE: the amplifier's input (V5 grid leak)
    COUT=300e-12,   # ESTIMATE: what loads the output - the multi-pin cable to the separate amplifier case plus the
                    # 6J5's input (Miller) capacitance. With C30 it makes a flat capacitive divider, so the
                    # fundamental reaches the amplifier (without it, 100 pF into 1 MOhm is a 1.6 kHz high-pass)
    # Inductances: ESTIMATES chosen from the printed capacitors so the resonances land in the audio band:
    #   L5 with C30 + COUT (400 pF) -> ~2 kHz peak, with C18 / C19 -> notches ~290 / 580 Hz;
    #   chain La+Lb (+2M) with C20 / C21 -> ~800 / 460 Hz; La with C27 -> ~800 Hz; Lc with C22 / C23 -> ~1.1 / 1.6 kHz
    L5=15.0, R_L5=1e3, RP_L5=2e6,           # ESTIMATE: L5, its copper, its core loss (in parallel)
    LA=2.0, LB=2.0, LC=2.0,                 # ESTIMATE: windings L4+L2 (IN-NB), L1 (NB-N1), L3 (secondary)
    RWA=200.0, RWB=200.0, RWC=200.0,        # ESTIMATE: their copper
    RPA=60e3, RPB=60e3, RPC=60e3,           # ESTIMATE: core loss across each winding (Q ~ 6 near 800 Hz)
    K=0.95,                                 # ESTIMATE: coupling between any two windings (one core), same sense
    R32_MAX=1e6, R32_MIN=50.0,              # ESTIMATE: the knee lever's track (lever step 1..64), log taper
    LEAK=1e-9,                              # 1 nS from every node to ground (numerical safety, inaudible)
)
LEVER_MAX = 64


def r32(lever: int) -> float:
    """The knee lever as a resistance across IN. Step 0: 10 ohm (silent); 1..64: log taper (ESTIMATE)."""
    if lever <= 0:
        return 10.0
    x = lever / LEVER_MAX
    return P["R32_MAX"] * (P["R32_MIN"] / P["R32_MAX"]) ** (1.0 - x)


def has(cfg, name):
    return (cfg >> BIT[name]) & 1


# The chip runs two simplifications (docs/ENGINE.md), measured against the drawing in tools/plot_net.py's terms:
#   the 0.02 uF coupling capacitor C5 becomes a first-order high-pass on the source (CLAV_HP_HZ), with R5 at IN:
#   within 0.8 dB from the lowest note (87 Hz) up; C17 (+ R29, R30) is left out while V is up: within 0.04 dB.
CLAV_HP_HZ = 1.0 / (2 * np.pi * 0.02e-6 * (100e3 + 12e3))


def elements(cfg: int, lever: int, full: bool = False):
    """Two-terminal elements as (kind, n1, n2, value): kind R (ohm) or C (farad). Coupled windings separately."""
    s = lambda n: has(cfg, n)
    e = [("R", ND["A"], GND, P["R5"]), ("C", ND["A"], ND["IN"], P["C5"])] if full else [("R", ND["IN"], GND, P["R5"])]
    e += [
         ("C", ND["IN"], ND["OUT"], P["C30"] + s("7") * P["C24"] + s("8") * P["C25"] + s("9") * P["C26"]),
         ("R", ND["a"], ND["NV"], P["R29"]), ("R", ND["NV"], GND, P["R30"]),
         ("C", ND["IN"], GND, P["C28"]), ("R", ND["IN"], ND["b"], P["R31"]), ("C", ND["b"], GND, P["C29"]),
         ("R", ND["IN"], GND, r32(lever)), ("R", ND["OUT"], GND, P["RIN"]), ("C", ND["OUT"], GND, P["COUT"]),
         ("R", ND["OUT"], ND["N1"], P["RP_L5"]), ("R", ND["IN"], ND["NB"], P["RPA"]),
         ("R", ND["NB"], ND["N1"], P["RPB"]), ("R", ND["N3"], GND, P["RPC"])]
    if s("V") or full:
        e.append(("C", ND["IN"], ND["a"], P["C17"]))
    if s("V"):
        e.append(("R", ND["NV"], GND, P["R_SHORT"]))
    csh = s("1") * P["C18"] + s("2") * P["C19"]
    if csh:
        e.append(("C", ND["N1"], GND, csh))
    if s("O"):
        e.append(("R", ND["OUT"], ND["N1"], P["R28"]))
    cn1 = s("3") * P["C20"] + s("4") * P["C21"]
    if cn1:
        e.append(("C", ND["N1"], GND, cn1))
        e.append(("R", ND["N1"], GND, 1.0 / (s("3") / P["R26"] + s("4") / P["R27"])))
    cn3 = s("5") * P["C22"] + s("6") * P["C23"]
    if cn3:
        e.append(("C", ND["N3"], GND, cn3))
    if s("A"):
        e.append(("C", ND["NB"], GND, P["C27"]))
    if s("B"):
        e.append(("R", ND["NB"], GND, P["R_SHORT"]))
    return e


WINDINGS = [(ND["IN"], ND["NB"]), (ND["NB"], ND["N1"]), (GND, ND["N3"])]   # (from, to) of La, Lb, Lc


def lmatrix():
    L = np.array([P["LA"], P["LB"], P["LC"]])
    M = P["K"] * np.sqrt(np.outer(L, L))
    np.fill_diagonal(M, L)
    return M


def rw():
    return np.diag([P["RWA"], P["RWB"], P["RWC"]])


def stamp(Y, a, b, y):
    if a >= 0: Y[a, a] += y
    if b >= 0: Y[b, b] += y
    if a >= 0 and b >= 0:
        Y[a, b] -= y; Y[b, a] -= y


def stamp_coupled(Y, ports, Yp):
    """Yp: admittance matrix between winding currents and winding voltages (i = Yp v)."""
    for k, (pk, qk) in enumerate(ports):
        for l, (pl, ql) in enumerate(ports):
            y = Yp[k, l]
            for (r, sr) in ((pk, 1), (qk, -1)):
                for (c, sc) in ((pl, 1), (ql, -1)):
                    if r >= 0 and c >= 0:
                        Y[r, c] += sr * sc * y


def ac(cfg: int, lever: int, f, full: bool = False):
    """V_OUT / I_V2 (ohm) at frequencies f (Hz). full=False: the network the chip runs (source into IN; the
    coupling high-pass applied separately, not included here); full=True: as drawn (source into node A, C5)."""
    f = np.atleast_1d(np.asarray(f, float))
    out = np.zeros(len(f), complex)
    Lm, R = lmatrix(), rw()
    els = elements(cfg, lever, full)
    for i, fi in enumerate(f):
        s = 2j * np.pi * fi
        Y = np.zeros((N, N), complex)
        for n in range(N):
            Y[n, n] += P["LEAK"]
        for kind, a, b, v in els:
            stamp(Y, a, b, 1.0 / v if kind == "R" else s * v)
        stamp(Y, ND["OUT"], ND["N1"], 1.0 / (P["R_L5"] + s * P["L5"]))
        stamp_coupled(Y, WINDINGS, np.linalg.inv(R + s * Lm))
        J = np.zeros(N, complex); J[ND["A" if full else "IN"]] = 1.0
        out[i] = np.linalg.solve(Y, J)[ND["OUT"]]
    return out


def ac_warped(cfg, lever, f):
    """What the bilinear-transform (trapezoidal) discrete model gives at f: the analog response at the warped
    frequency (2/T) tan(pi f T) / 2 pi."""
    fw = np.tan(np.pi * np.asarray(f) * T) / (np.pi * T)
    return ac(cfg, lever, fw)


def companion(T=T):
    """Constants for the trapezoidal companion models (SI units), at sample period T."""
    Lm, R = lmatrix(), rw()
    Zp = R + (2.0 / T) * Lm                     # i[n] = Gm (v[n] + v[n-1]) + Am i[n-1]
    Gm = np.linalg.inv(Zp)
    Am = Gm @ ((2.0 / T) * Lm - R)
    zl = P["R_L5"] + 2.0 * P["L5"] / T
    return dict(Gm=Gm, Am=Am, g_l5=1.0 / zl, a_l5=(2.0 * P["L5"] / T - P["R_L5"]) / zl)


def cfg_of(*names):
    c = 0
    for n in names:
        c |= 1 << BIT[n]
    return c


if __name__ == "__main__":                      # a quick look: peak frequency and level of a few combinations
    f = np.geomspace(50, 15000, 400)
    for name, c in [("none (Trumpet)", cfg_of()), ("1 O (Violin)", cfg_of("1", "O")), ("3 (Trombone)", cfg_of("3")),
                    ("4 (Tenor Sax)", cfg_of("4")), ("1 4 8 (Oboe)", cfg_of("1", "4", "8")),
                    ("3 4 5 (Flute)", cfg_of("3", "4", "5")), ("3 B (Saw)", cfg_of("3", "B")),
                    ("2 3 (Horn)", cfg_of("2", "3")), ("3 7 (Bassoon)", cfg_of("3", "7"))]:
        h = 20 * np.log10(np.abs(ac(c, 64, f)))
        k = np.argmax(h)
        print(f"{name:16s} peak {f[k]:7.0f} Hz  {h[k]:6.1f} dB(ohm)   at 200 Hz {np.interp(200, f, h):6.1f}"
              f"   1 kHz {np.interp(1000, f, h):6.1f}   5 kHz {np.interp(5000, f, h):6.1f}")
