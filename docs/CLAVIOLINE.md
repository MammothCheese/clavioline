# CLAVIOLINE: status, controls, parameters

The research behind this file is in [CLAVIOLINE-ANALYSIS.md](CLAVIOLINE-ANALYSIS.md): the Figure 4 netlist, the
tablets, the sources and the open questions.

## Controls

### PLAY mode

| Control | Does | Historical part |
|---|---|---|
| 27 keys, MIDI notes in | Monophonic, highest note wins, legato never re-attacks, the oscillator restarts from rest after silence | Fig. 2 key ladder, the shared gate contact |
| PRESETS | Knee lever, 64 steps, accelerating; step 0 = silent. MIDI CC 11 moves it too | R32, **inside the tone network**: it changes the tone as well as the level |
| MASTER | Volume | (platform) |
| OCT− / OCT+ | Coupler LOW / MED / HIGH | S1 |
| ALGORITHM | Keyboard window +0 / +1 octave (27 keys over the original's 36) | FM-1 only |
| SELECT | Presets: 17 from the 1952 Table 1, 22 Selmer voicings quoted by Sound On Sound, a Concert sub-octave organ, 4 user slots. `*` = edited | Table 1 |
| SAVE | Store the tablets and the coupler in the user slot shown | |
| LFO | Vibrato off → I (4.5 Hz) → II (5.5) → III (6.5) | S3, S4, S5 |
| FX | Vibrato amplitude (AMP) | S6 |
| ENV | Tablet P (short attack) | S2 |
| EDIT | STOPS mode | |
| KNOB 1 | Fine tune ±50 cents | VR6 |

### STOPS mode (EDIT toggles it, HOME also leaves it; nothing is lost)

```
 black:     F#3  G#3        C#4  D#4        F#4  G#4  A#4   C#5
            Ip   IIp        If   IIf        I    II   III   AMP
 white:  F3  G3  A3  B3  C4  D4  E4  F4  G4  A4  B4  C5  D5  E5  F5
          1   2   3   4   5   6   7   8   9   O   A   B   V   P   all off
```

- Key LEDs show the tablets that are down.
- **PLAY** holds the last note as a drone while you change tablets, so you hear each change at once. MIDI notes keep playing.
- Sub I / Sub II are the Concert's octave dividers: **p** is soft, **f** is loud, and they mix with the main tone.
- The vibrato speed tablets work independently, like the real ones. When several are down, the fastest wins. That rule is not documented anywhere.

The screen shows the tablets in two rows like the front rail, with pressed ones lit and set lower. Below them are the
preset name, the note, the coupler, the knee lever, and the window and tune.

## The sound engine, and what it rests on

| Part | Model | Basis |
|---|---|---|
| Pitch | 12-TET phase accumulator; coupler = exact octaves | 1952 article (½ % resistors) |
| Waveform | Trapezoid, 50 % duty, 40 µs edges fixed in time, polyBLAMP anti-aliasing | "trapezoidal" (1952); "similar to a square wave" (SOS); **edge time ESTIMATE** |
| Sub I / II | Flip-flop dividers (÷2, ÷4) clocked by the oscillator, square waves with polyBLEP; p / f levels | Concert drawing DRG 1289 (V5, V6, switches I p, II p, I f, II f); SOS; **levels ESTIMATE** (f = main tone, p = −10 dB) |
| Gate | Exact per-sample discretisation of the printed V2 cathode network; P switches it | **DERIVED**: 19.2 ms normal, 1.54 ms with P |
| Gate gain | Remote-cut-off curve, e^(−v/0.12), normalised | 6BA6 is variable-µ; **curve ESTIMATE**; absolute bias levels don't add up from printed values (U11) |
| Key contacts | Oscillator 3 ms before the gate on press; gate 4 ms before the oscillator stops on release | "a very slight delay" (1952, Gibson restorer); **times ESTIMATE** |
| **Tone network** | **The whole Fig. 4 netlist** (23 elements, coupled L1–L4, L5), bilinear companion models, DK form: `v = KC·xc + KL·hl + KU·i`. Gains are recomputed in software floating point in the main loop when a tablet or the lever changes. The audio interrupt is integer only | Topology and all R/C values **PRINTED**; V2 anode load 100 kΩ and coupling 0.02 µF from the **Concert drawing**; inductances, coil losses, coupling, amplifier input **ESTIMATE** |
| Knee lever | R32 across the network input, log taper 50 Ω .. 1 MΩ, 10 Ω at step 0 | Position PRINTED; **taper and range ESTIMATE** |
| Vibrato | Sine LFO; FM ±8.4 / ±32 cents (AMP off / on), plus ±3 % AM | Rates PRINTED; depth ratio 3.8 **DERIVED** (R23/R24); absolute depth and AM **ESTIMATE**; waveform **ESTIMATE** |
| Amplifier | y + 0.35·y², soft ceiling, DC blocker | "most of the distortion … 2nd harmonic" (1952); "a large amount of distortion is deliberately obtained" (Selmer manual, via SOS); **amount ESTIMATE** |
| Tablet change | 2.9 ms fade out, switch, 2.9 ms fade in | Not historical (real tablets click) |

The estimated inductances were chosen from the printed capacitors so that each resonance lands in the audio band:

| Coil | Estimate | Resonances it gives with printed capacitors |
|---|---|---|
| L5 | 15 H (1 kΩ copper, 2 MΩ core loss) | Formant ~4 kHz with C30 (100 pF), shifting down with 7/8/9; notches ~290 / 580 Hz with C18 / C19 |
| L4+L2 (one winding), L1, L3 | 2 H each, k = 0.95, 200 Ω copper, 60 kΩ core loss | ~800 / 460 Hz with 3 / 4; ~800 Hz with A; ~1.1 / 1.6 kHz with 5 / 6 on the secondary |
| Amplifier input | 1 MΩ | Sets the high-pass of the 100 pF output path |

Every value lives in `tools/clav_net_ref.py` (the network) or `tools/gen_clav_tables.py` (gate, tables). Change one, run
`python3 tools/gen_clav_tables.py`, then rebuild. The responses for twelve tablet sets are plotted in
[net-responses.png](net-responses.png); `tools/plot_net.py` redraws it.

What the model shows about the original (given the estimates):
- Tablets 1 and 2 make the strongest changes: a notch and a formant above it.
- **O** turns them into a muted, quieter tone. This fits Selmer's "Muted Gypsy Violin" using O.
- The transformer tablets (3, 4, 5, 6, A, B) colour the tone more gently, because the printed 20 kΩ (R31) loads that node heavily.
- 5 and 6 are inert unless A, B, 3 or 4 is also down, exactly as the drawing says. So **Cornet (6 alone) sounds like Trumpet** here. Two independent Selmer tables list Cornet as 6 alone, so something on the real instrument differs from the 1952 drawing. This is still open.

## Measured on the host (`tests/run_engine.sh`)

| Check | Result |
|---|---|
| Pitch C4, C5, coupler HIGH; highest-note priority; return to the held note | within ±0.3 cents |
| Legato note change | no re-attack (level changes 4–5 dB only because the octave moves harmonics through the network) |
| Attack, normal / P: to −40 / −20 / −3 dB | 14 / 26 / 62 ms, then 2 / 4 / 6 ms |
| Knee lever 48 → 8 steps | −38 dB |
| Alias energy, a 3136 Hz note (network and valve included) | −55.5 dB below the harmonics |
| Vibrato I / II / III | 4.49 / 5.52 / 6.49 Hz |
| Vibrato depth with AMP | 3.84× the shallow depth |
| Sub I f | the octave below at +1 dB re the fundamental |
| **Chip network vs the reference model** (29 tablet / lever cases, white noise) | within 0.3 dB wherever the response is within 40 dB of its peak. The one exception is at the quietest settings, where the difference sits 85 dB or more under the loudest sound (the integer noise floor) |

### Cost

The audio interrupt per sample is roughly:
- 10 nodes × 14 multiply-accumulates (32 × 32 → 64) for the network;
- 9 capacitor and 4 inductor updates;
- the gate (9 multiplies);
- the trapezoid with polyBLAMP, about 4 corner tests;
- the valve stage.

That puts it near Dinghy's yardstick of the sloopDX engines (~1000–1500 host instructions a sample), which fit. **It has
not been timed on an FM-1.** Time it with `fm1_ticks()` around `app_audio`.

## Not yet verified

- **Nothing has run on an FM-1** yet. That means no audio-interrupt timing, and no test of rescue, update or settings on hardware.
- The `.fwsc` is built by GitHub Actions on every push to `main` (the run's artifact). It was never built on this PC.
- Identity `FM-1_950`: check that 95N is free on the FM-1 Firmware Hub before a release.

## Host commands

```sh
sh tests/run_engine.sh                      # everything above (CC=gcc PYTHON=python on Windows/MinGW)
sh tests/run_host.sh tests/scripts/stage4.txt   # WAV + screen PNGs in build/host/
python3 tools/clav_net_ref.py               # a quick look at the reference responses
python3 tools/gen_clav_tables.py            # after changing an estimate
```
