# The sound engine

How the firmware makes the sound, which numbers come from the original drawings and which are estimates, and how it
is tested. The history behind it, including why the engine looks the way it does, is in [HISTORY.md](HISTORY.md).

## Signal chain

```
                 22.05 kHz                                                                   44.1 kHz
keys ─► pitch ─► multivibrator ─► control valve ─► coupling ─► formant filter ─► knee ─► ┊ ─► valve amp ─► speaker ─► out
        vibrato  wavetables       attack (P)       high-pass   5 biquads per      lever  ┊    2nd harmonic  cabinet
                 + Sub I / II     sustains         (C5)        tablet combination        ┊
```

The instrument runs at 22.05 kHz, one step for every two output samples. A 4-point half-band interpolator
`[-1 9 9 -1] / 16` brings it to 44.1 kHz for the amplifier and the speaker, so the amplifier's distortion does not alias.
After 100 ms of silence the engine rests and does no work until a key goes down.

| Stage | How | Source |
|---|---|---|
| Notes | A count per MIDI note from keys, MIDI in and the STOPS drone; the highest wins. The pitch changes at once; the gate stays open while any note is held, so legato never re-attacks. After silence the oscillator restarts from rest | Fig. 2 key ladder and contacts (documented) |
| Oscillator | Band-limited wavetables, one per octave (harmonics up to 10.5 kHz), of the multivibrator's wave as the control valve passes it on: a sharp drop, an exponential recovery, uneven clipping | "trapezoidal in form" (documented); the shape is an **estimate** (`tools/clav_wave.py`) |
| Sub I / II | Square wavetables at a half and a quarter of the oscillator's phase (the dividers flip on its periods); **p** and **f** levels | Concert drawing (documented); levels **estimated** |
| Vibrato | Sine LFO at 4.5 / 5.5 / 6.5 Hz; pitch ±8.4 cents, ±32 with AMP; ±3 % amplitude | Rates documented, depth ratio derived; depth and AM **estimated** |
| Control valve (attack) | The printed cathode network, discretised exactly over 8 output samples; tablet P selects the short time constant; gain through a remote-cut-off curve. Notes sustain while held | Time constants derived (19.2 ms; 1.54 ms with P); curve **estimated** |
| Coupling | C5 (0.02 µF) as a first-order high-pass at 71 Hz | Concert drawing |
| Formant filter | Five biquads, decoded for the tablets down (below) | Fig. 4 model |
| Knee lever | A gain per lever step: the model's level change with R32, smoothed over ~6 ms | Position documented; taper **estimated** |
| Valve amplifier | y + 0.59 y², a soft ceiling: the 2nd harmonic well up at full swell, less when softer | "mostly 2nd harmonic", "a large amount of distortion is deliberately obtained" (documented); amount **estimated** |
| Speaker and cabinet | Three biquads: high-pass 100 Hz Q 1.1, +4 dB at 2.2 kHz Q 1, low-pass 5.5 kHz | A 10-inch speaker "specially produced to add its measure" (documented); response **estimated** |
| DC blocker | ~14 Hz | |

## The formant filter

The tone network's model (`tools/clav_net_ref.py`, the netlist in [HISTORY.md](HISTORY.md)) is solved on a computer,
not on the FM-1:

1. `tools/fit_formants.py` computes the exact response of the network for each of the **8192 combinations** of tablets
   1–9, O, A, B, V (knee lever open).
2. It fits each response with **five second-order sections** by vector fitting, then stores each section as its zero
   and pole frequencies and Qs (6 bytes): `firmware/src/clav_formants.h`, 262 KB of flash.
3. On the FM-1, when a tablet changes, `fm_compute()` decodes the combination and turns each section into a biquad
   (bilinear transform at 22.05 kHz) in a small software floating point, scaling each to unit peak gain. The audio
   interrupt runs the five biquads in 32-bit fixed point with 64-bit sums. A tablet change fades out and in over 3 ms.

The tablets interact exactly as the model says, because every combination is solved as a whole. Splitting them into
independent groups was measured and rejected: the ladder's sections load each other, and the error was 12 dB on a
typical combination. Four sections per combination were not enough (90 % within 1.2 dB, the rest up to 14 dB off).

**Accuracy**, against the model, 50 Hz–9 kHz wherever the response is within 40 dB of its peak:
- the stored fit, all 8192 combinations: median 0.36 dB, 99 % within 1.09 dB, worst 3.61 dB;
- the FM-1's own code end to end (fit, rounding, decoding, fixed point), on Selmer's voicings and random combinations:
  within 0.60 dB on all 48 tested.

[net-responses.png](net-responses.png) shows the model's responses for twelve tablet sets;
`python3 tools/plot_net.py docs/net-responses.png` redraws it.

## Parameters

Each value is tagged by where it comes from: **printed** (legible on the 1952 drawings), **Concert** (legible on
Selmer's Concert drawing), **derived** (follows from printed values) or **estimate** (chosen, because no document
gives it). After changing a network value, rerun `python3 tools/fit_formants.py` (about 5 minutes) and
`python3 tools/gen_clav_tables.py`, then rebuild.

### The network (`tools/clav_net_ref.py`)

| Value | Setting | Source |
|---|---|---|
| All capacitors and resistors of Fig. 4 | as drawn | Printed |
| C17 (tablet V) | 0.01 µF | Concert, faint (".1" or ".01"); the 1952 "100pF" label would make V inaudible ([HISTORY.md](HISTORY.md)) |
| Control valve anode load | 100 kΩ | Concert |
| L5 | 15 H, 1 kΩ copper, 5 MΩ core loss | **Estimate**: tablets 1 / 2 near 290 / 580 Hz |
| L4 + L2, L1, L3 | 2 H each, coupling 0.95, 100 Ω copper, 150 kΩ core loss (Q ~ 15) | **Estimate**: tablets 3 / 4 near 800 / 460 Hz, A near 800 Hz, 5 / 6 near 1.1 / 1.6 kHz |
| Amplifier input | 1 MΩ | **Estimate** |
| Cable and amplifier input capacitance | 300 pF | **Estimate** |
| Knee lever (R32) | 50 Ω … 1 MΩ, log taper over 64 steps; silent at rest | Position printed; **range and taper estimated** |

Two parts of the drawing are simplified in the model: C5 is the coupling high-pass outside the network (within 0.8 dB
from the lowest note, 87 Hz, up), and C17 is left out while V is up (within 0.07 dB).

### Oscillator, attack, vibrato, amplifier (`firmware/src/clavioline.c`, `tools/clav_wave.py`, `tools/gen_clav_tables.py`)

| Value | Setting | Source |
|---|---|---|
| Wave shape | conducting 30 % of the period, recovery time constant 10 % of it, clip 2.2 with bias 0.25 | **Estimate**: harmonics 1–5 at 0, −13, −14, −15, −21 dB |
| Attack time constants | 19.2 ms; 1.54 ms with P | Derived from the printed cathode network |
| Valve turn-on curve | gain e^(−v/0.12) | **Estimate** |
| Key contact delays | oscillator 3 ms before the gate; gate 4 ms before the oscillator stops | **Estimate** |
| Sub I / II levels | f = the main tone's level, p = 10 dB softer | **Estimate** |
| Vibrato | 4.5, 5.5, 6.5 Hz; ±8.4 / ±32 cents; ±3 % AM; the fastest of several wins | Rates printed; ratio derived; the rest **estimated** |
| Amplifier | d = 0.59 | **Estimate** |
| Speaker | see the signal chain | **Estimate** |
| Pitch | the FM-1's F3 key = F3 with the coupler at MED | **Estimate** |

## Options (not on the original)

**SEQ** opens the options page; **SEQ** or **HOME** closes it. **SELECT** chooses a setting and **KNOB 1** changes it.

| Setting | Values | What it does |
|---|---|---|
| PLUCK DECAY | **OFF** (default), 0.3, 0.6, 1.2, 2.5 s | Each key press starts at full level, then fades 40 dB over the time chosen, standing in for flicking the knee lever back |
| DECAY ON | **P NOTES** (default), ALL NOTES | Only while tablet P is down, or on every note |

## Tests

```sh
sh tests/run_engine.sh        # on Windows with MinGW: CC=gcc PYTHON=python sh tests/run_engine.sh
```

It renders scripted performances (`tests/scripts/*.txt`) with the firmware's own code on the computer, measures them,
and compares the FM-1's formant filter with the model:

| Check | Result |
|---|---|
| Pitch of C4 and C5, highest-note priority, return to the held note, coupler | within ±0.1 cents |
| Legato note change | no new attack |
| Attack to −40 / −20 / −3 dB | 12 / 28 / 62 ms; with P 2 / 4 / 6 ms |
| With P, the note still sustains | +0.0 dB from 0.25 s to 0.5 s |
| Without P, a gradual start | first 40 ms at 0.32 of the steady peak |
| Knee lever from step 48 to 8 | −38.7 dB |
| Aliasing on the top note (3136 Hz) | −45.3 dB below the harmonics |
| Vibrato I / II / III | 4.49 / 5.52 / 6.49 Hz; deep / shallow 3.65 |
| STOPS mode note buttons | SEL plays D4 (293.7 Hz) |
| Sub I f | the octave below at +3.1 dB re the fundamental |
| Pluck decay 1.2 s (P notes, all notes); off | 37.2 dB down from 0.2 s to 1.3 s; off 0.0 dB |
| The FM-1's formant filter against the model | within 0.60 dB (48 combinations) |

`tests/run_tests.sh` adds the platform's update-path tests after a firmware build. `tests/scripts/demo.txt` plays a
phrase on six presets: `tests/run_host.sh tests/scripts/demo.txt` writes it to `build/host/clavioline.wav`.

### CPU load

The audio interrupt times itself against the 5.8 ms it has for each 256-sample block. STOPS mode shows the load on the
bottom line (`CPU nn%  PEAK nn%`, including the platform's own interrupts that land inside a render); if the peak passes
90 %, PLAY mode shows `AUDIO OVERLOAD`. Per output sample the engine does roughly 40 multiplies (half of the
instrument's work at 22.05 kHz, the amplifier and speaker at 44.1 kHz), against about 150 when it simulated the
network.

## Building

GitHub Actions builds `clavioline.fwsc` on every push to `main` (the run's artifact) and makes a release on a tag
`vX.Y`. Locally you need Linux (or WSL or Docker), the JieLi toolchain and three files from the JieLi AC79 SDK:

```sh
pip3 install Pillow numpy
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
./build.sh                    # -> build/clavioline.fwsc (identity FM-1_950)
./build.sh --release 1.0      # -> build/clavioline-1.0.fwsc
```

The build refuses code the FM-1 cannot run: floating point, 64-bit division, calls into the chip ROM, too much RAM.
[platform/RULES.md](platform/RULES.md) explains why; [platform/API.md](platform/API.md) describes the platform.

## Files

| Path | What |
|---|---|
| `firmware/src/clavioline.c` | The instrument: voice, oscillator, attack, vibrato, sub-octaves, amplifier, speaker, controls, screen, presets |
| `firmware/src/clav_formant.h` | The formant filter on the chip: decoding, software floating point, biquads |
| `firmware/src/clav_formants.h` | The fitted filters for all 8192 tablet combinations (generated) |
| `firmware/src/clav_tables.h` | Wavetables, attack matrices, lever gains, speaker filter, tuning (generated) |
| `tools/clav_net_ref.py` | The network's netlist and values, and its exact analysis |
| `tools/fit_formants.py` | Fits every tablet combination and writes `clav_formants.h` |
| `tools/clav_wave.py` | The oscillator's wave shape |
| `tools/gen_clav_tables.py` | Writes `clav_tables.h` |
| `tools/plot_net.py` | Plots network responses |
| `tests/` | Host scripts and checks |
| everything else | The FM-1 platform (Dinghy): USB, updates, rescue, MIDI, screen, keys, flash |
