#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Plot the reference network's response for tablet combinations: tools/plot_net.py OUT.png [lever]"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
sys.path.insert(0, __file__.rsplit("/", 1)[0] if "/" in __file__ else ".")
from clav_net_ref import ac, cfg_of

SETS = [("none (Trumpet)", ()), ("1 O (Violin)", ("1", "O")), ("1 V", ("1", "V")), ("3 (Trombone)", ("3",)),
        ("4 (Tenor Sax)", ("4",)), ("1 4 8 (Oboe)", ("1", "4", "8")), ("3 4 5 (Flute)", ("3", "4", "5")),
        ("3 B (Musical Saw)", ("3", "B")), ("2 3 (Horn)", ("2", "3")), ("3 7 (Bassoon)", ("3", "7")),
        ("6 (Cornet)", ("6",)), ("3 6 8 (Mandolin)", ("3", "6", "8"))]
out = sys.argv[1]
lever = int(sys.argv[2]) if len(sys.argv) > 2 else 64
f = np.geomspace(40, 16000, 3000)
fig, axs = plt.subplots(3, 4, figsize=(16, 10), sharex=True, sharey=True)
ref = 20 * np.log10(np.abs(ac(0, lever, f))).max()
for ax, (name, t) in zip(axs.flat, SETS):
    ax.semilogx(f, 20 * np.log10(np.abs(ac(cfg_of(*t), lever, f))) - ref)
    ax.set_title(name); ax.grid(True, which="both", alpha=.3); ax.set_ylim(-60, 15)
fig.suptitle(f"Fig. 4 reference model, V_OUT / I_V2, lever {lever} (0 dB = no tablets' peak). Inductances are ESTIMATES")
fig.tight_layout(); fig.savefig(out, dpi=80)
print(out)
