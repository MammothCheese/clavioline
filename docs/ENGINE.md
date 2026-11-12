# The sound engine

How the firmware makes the sound, which numbers come from the original drawings and which are estimates, and how it
is tested. The history behind it is in [HISTORY.md](HISTORY.md).

## Signal chain

```
keys / MIDI ─► highest note ─► oscillator (trapezoid) ─┬─► control valve (V2) ─► tone-forming network ─► amplifier ─► out
               coupler, tune     vibrato FM + AM       │    attack from the        Fig. 4, every part,      2nd-harmonic
                                                      │    printed cathode        knee lever inside        distortion
               Concert Sub I / II dividers ───────────┘    network, tablet P
```

| Stage | File | How |
|---|---|---|
| Notes | `firmware/src/clavioline.c` | A count per MIDI note from keys, MIDI in and the STOPS drone; the highest wins. The pitch changes at once, and the gate stays open while any note is held |
| Oscillator | `clavioline.c` | A 32-bit phase accumulator. Trapezoid with 50 % duty and edges of fixed duration, anti-aliased with polyBLAMP at its four corners |
| Sub-octaves | `clavioline.c` | Flip-flops toggled on each oscillator period (÷2, ÷4), square waves with polyBLEP; **p** and **f** levels |
| Control valve | `clavioline.c`, tables from `tools/gen_clav_tables.py` | The cathode network (C1–C3, R1, R3, R4, R7, R8) discretised exactly per sample; tablet P selects the other matrix. Gain follows a remote-cut-off curve |
| Tone network | `firmware/src/clav_net.h`, reference `tools/clav_net_ref.py` | Below |
| Amplifier | `clavioline.c` | y + d·y², a soft ceiling (1.5u − 0.5u³), DC blocker |

## The tone network

The network is simulated element for element. The netlist is in [HISTORY.md](HISTORY.md) and in `tools/clav_net_ref.py`.

- **Discretisation:** every capacitor and coil is a trapezoidal (bilinear) companion model.
  - The four coupled windings use their inductance matrix.
  - Coil losses are a series copper resistance and a parallel core-loss resistance.
- **Per sample:** the network is solved in DK form, `v = KC·xc + KL·hl + KU·i`, with capacitor voltages and coil currents as the states.
  - That is 9 node voltages × 15 multiply-accumulates, all in 32-bit integers with 64-bit sums.
- **When the gains change:** the gains (KC, KL, KU) depend on the tablets and the knee lever. When either changes, the main loop:
  - rebuilds the conductance matrix exactly in integers;
  - inverts it in a small software floating point (the chip has no FPU);
  - hands the new gains to the audio interrupt.
- **Tablet changes** fade out and in over 3 ms each way.

**Accuracy:** `tests/net_check.py` drives the chip's code with white noise and compares the result with the exact
analysis of the same netlist, frequency warping included. Across 29 tablet and lever combinations it agrees within
**0.31 dB** wherever the response is within 40 dB of its peak. What remains lies 80 dB or more below the loudest sound.

The responses for twelve tablet sets, with the current estimates, are in [net-responses.png](net-responses.png).
`python3 tools/plot_net.py docs/net-responses.png` redraws it.

## Parameters

Each value is tagged by where it comes from:
- **Printed**: legible on the 1952 drawings;
- **Concert**: legible on Selmer's Concert drawing;
- **Derived**: follows from printed values;
- **Estimate**: chosen, because no document gives it.

Change any value, then run `python3 tools/gen_clav_tables.py` and rebuild.

### The network (`tools/clav_net_ref.py`)

| Value | Setting | Source |
|---|---|---|
| All capacitors and resistors of Fig. 4 | as drawn | Printed |
| Control valve anode load, coupling capacitor | 100 kΩ, 0.02 µF | Concert |
| L5 | 15 H, 1 kΩ copper, 2 MΩ core loss | **Estimate**: puts tablets 1 / 2 near 290 / 580 Hz |
| L4 + L2, L1, L3 | 2 H each, coupling 0.95, 200 Ω copper, 60 kΩ core loss | **Estimate**: puts tablets 3 / 4 near 800 / 460 Hz, A near 800 Hz, 5 / 6 near 1.1 / 1.6 kHz |
| Amplifier input | 1 MΩ | **Estimate** |
| Cable and amplifier input capacitance | 300 pF | **Estimate**: the multi-pin cable to the separate amplifier cabinet plus the input valve's Miller capacitance |
| Knee lever (R32) | 50 Ω … 1 MΩ, log taper over 64 steps; 10 Ω at rest (silent) | Position printed; **range and taper estimated** |
| Closed B or V contact | 100 Ω | Stands in for a short |

### The oscillator, gate and vibrato (`firmware/src/clavioline.c`, `tools/gen_clav_tables.py`)

| Value | Setting | Source |
|---|---|---|
| Attack time constants | 19.2 ms; 1.54 ms with P | Derived from the printed cathode network |
| Valve turn-on curve | gain e^(−v/0.12) | **Estimate** (remote-cut-off valve) |
| Key contact delays | oscillator 3 ms before the gate; gate closes 4 ms before the oscillator stops | **Estimate** |
| Trapezoid | 50 % duty, 100 µs edges | Duty from "similar to a square wave"; **edge estimated** |
| Sub I / II levels | f = the main tone's level, p = 10 dB softer | **Estimate** |
| Vibrato rates | 4.5, 5.5, 6.5 Hz | Printed |
| Vibrato depth | ±8.4 cents, ±32 cents with Amplitude | Ratio derived (3.8 : 1); **depth estimated** |
| Vibrato amplitude modulation | ±3 % | **Estimate** ("a small amount") |
| Several speed tablets down | the fastest wins | **Estimate** (undocumented) |
| Amplifier | d = 0.35 | **Estimate** ("a large amount of distortion is deliberately obtained") |
| Pitch | the FM-1's F3 key = F3 with the coupler at MED | **Estimate** |

## Tests

```sh
sh tests/run_engine.sh        # on Windows with MinGW: CC=gcc PYTHON=python sh tests/run_engine.sh
```

This renders scripted performances (`tests/scripts/*.txt`) with the firmware's own code on the computer and measures
them:

| Check | Result |
|---|---|
| Pitch of C4 and C5, highest-note priority, return to the held note, coupler | within ±0.2 cents |
| Legato note change | no new attack (0.6 dB level change) |
| Attack to −40 / −20 / −3 dB | 12 / 26 / 62 ms; with P 2 / 4 / 6 ms |
| Knee lever from step 48 to 8 | −38 dB |
| Aliasing on a 3136 Hz note | −56.5 dB below the harmonics |
| Vibrato I / II / III | 4.49 / 5.52 / 6.49 Hz; deep / shallow depth 3.78 |
| Sub I f | the octave below at the level of the fundamental |
| Chip network against the exact analysis | within 0.31 dB |

`tests/run_tests.sh` adds the platform's update-path tests (the installer, the update loader) after a firmware build.

**Listening:** `tests/scripts/demo.txt` plays a phrase on six presets. Render it with:

```sh
tests/run_host.sh tests/scripts/demo.txt
```

The sound goes to `build/host/clavioline.wav`, with screenshots alongside.

## Building

GitHub Actions builds `clavioline.fwsc` on every push to `main` (the run's artifact), and makes a release on a tag `vX.Y`.

To build locally you need Linux (or WSL or Docker), the JieLi toolchain and three files from the JieLi AC79 SDK:

```sh
pip3 install Pillow numpy
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
./build.sh                    # -> build/clavioline.fwsc (identity FM-1_950)
./build.sh --release 1.0      # -> build/clavioline-1.0.fwsc
```

The build refuses code the FM-1 cannot run: floating point, 64-bit division, calls into the chip ROM, too much RAM.
[platform/RULES.md](platform/RULES.md) explains why, and [platform/API.md](platform/API.md) describes the platform's functions.

## Files

| Path | What |
|---|---|
| `firmware/src/clavioline.c` | The instrument: voice, oscillator, gate, vibrato, sub-octaves, amplifier, controls, screen, presets |
| `firmware/src/clav_net.h` | The tone network on the chip, and its software floating point |
| `firmware/src/clav_tables.h` | Generated tables (do not edit) |
| `tools/clav_net_ref.py` | The network's netlist and values; the exact reference analysis |
| `tools/gen_clav_tables.py` | Writes `clav_tables.h` from the values |
| `tools/plot_net.py` | Plots network responses |
| `tests/` | Host scripts and checks |
| everything else | The FM-1 platform (Dinghy): USB, updates, rescue, MIDI, screen, keys, flash |
