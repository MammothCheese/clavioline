# CLAVIOLINE: status and parameters

The research behind this file is in [CLAVIOLINE-ANALYSIS.md](CLAVIOLINE-ANALYSIS.md) (the Figure 4 netlist, the
tablets, the sources, the open questions).

## Stage 2 (this build): the playable core

| Control | Does | Historical part |
|---|---|---|
| 27 keys, MIDI notes in | Monophonic, highest note wins, legato never re-attacks, the oscillator restarts from rest after silence | Fig. 2 key ladder, the shared gate contact, the 30 V hold-off bias |
| PRESETS | Knee lever, 64 steps, accelerating; step 0 = silent. MIDI CC 11 moves it too | R32 |
| MASTER | Volume | (platform) |
| OCT− / OCT+ | Coupler LOW / MED / HIGH, lit when not MED | S1 |
| ALGORITHM | Keyboard window +0 / +1 octave (the FM-1's 27 keys over the original's 36) | FM-1 only |
| ENV | Tablet P: the short attack, lit when on | S2 |
| KNOB 1 | Fine tune ±50 cents | VR6 |

The screen shows the 18-tablet rail (only P is live so far), the note, the coupler, the lever, the window and the tune.
The coupler, window, P and tune are saved to flash 2 s after a change, once silent. The lever is not saved.

Not in this build: the tone-forming network and the 13 tone tablets (Stage 3), vibrato, the valve amplifier, STOPS mode
and presets (Stage 4).

## What is modelled, and from what

| Part | Model | Basis |
|---|---|---|
| Pitch | 12-TET phase accumulator, coupler = exact octaves | 1952 article (½ % resistors); the exact coupler is an idealisation |
| Waveform | Trapezoid, 50 % duty, 40 µs linear edges fixed in time, polyBLAMP anti-aliasing | "trapezoidal" (1952); "similar to a square wave" (SOS); **edge time ESTIMATE** |
| Gate | Exact discretisation of the printed V2 cathode network (C1–C3, R1, R3, R4, R7, R8), P switching it | **DERIVED** time constants: 19.2 ms normal, 1.54 ms with P (and 0.5 / 0.1 ms minor poles) |
| Gate gain | Remote-cut-off curve, gain = e^(−v/0.12), normalised | 6BA6 is a variable-µ pentode; **curve ESTIMATE** (U6, U11) |
| Key contacts | Oscillator 3 ms before the gate on press; gate 4 ms before the oscillator stops on release | Two contacts with "a very slight delay" (1952, Gibson restorer); **times ESTIMATE** |
| Release | Gate recharges through R8 (≈ 225 ms); the oscillator stops 4 ms after key-up, with a 2 ms fade | Fade is **not historical** (anti-click) |
| Lever | Smoothed gain, 0 = silent, then −45..0 dB linear in dB | **taper ESTIMATE** (R32's grading not printed) |
| Absolute pitch | FM-1 F3 key = F3 at MED, window +0 | **ESTIMATE** |
| DC coupling | ~15 Hz high-pass | stands in for the coupling capacitors |

Every ESTIMATE lives in `tools/gen_clav_tables.py` (tables) or at the top of `firmware/src/clavioline.c`.

## Measured on the host (`tests/run_stage2.sh`)

| Check | Result |
|---|---|
| Pitch of C4, C5 (and coupler HIGH) | within ±1 cent (limited by the measurement window) |
| C4 + F3 held: the higher sounds; + C5: jumps up; C5 let go: back to C4 | pass |
| Level change across a legato note change | 0.2 dB (no re-attack) |
| Attack, normal: to −40 / −20 / −3 dB | 12 / 26 / 62 ms |
| Attack, P: to −40 / −20 / −3 dB | 2 / 4 / 6 ms |
| Knee lever 48 → 8 steps | −28.1 dB (as the table) |
| Alias energy, a 3136 Hz note | −50.6 dB below the harmonics (−30.9 dB without polyBLAMP) |

## Not yet verified

- **No `.fwsc` has been built.** The JieLi toolchain is Linux-only. Use WSL or Docker on Windows, or run the GitHub Actions
  workflow (`.github/workflows/release.yml`, FM-1 only now).
- Nothing has run on an FM-1: no ISR timing, no rescue or update-path test on hardware.
- Identity `FM-1_950`: check on the FM-1 Firmware Hub that 95N is free before a release.

## Host commands

```sh
sh tests/run_stage2.sh                    # render + measure (CC=gcc PYTHON=python on Windows/MinGW)
sh tests/run_host.sh tests/scripts/clav.txt   # WAV + screen PNGs in build/host/
python3 tools/gen_clav_tables.py           # after changing an estimate there
```
