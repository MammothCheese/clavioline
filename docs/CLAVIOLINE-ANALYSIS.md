# Clavioline for the M-VAVE FM-1: Stage 1 analysis

Sources used, in order of authority:

1. **G. H. Hillier, "The Clavioline", *Electronic Engineering*, October 1952, pp. 454–455** (Figs. 2–5, Table 1). This is the primary reference.
2. **C. Martin, US 2,563,477 (filed 1948, granted 1951).** This describes the principle (the multivibrator, the delayed "coupling tube" gate, the two attack modes, and a filter built on one transformer core). Its filter **topology is different** from the 1952 production circuit, so I use it only to confirm intent, never for values or wiring.
3. **Dinghy** (`zvenson/dinghy`, commit `ea1b6bf`, 7 Nov 2026). Only `.fwsc` builds were on this PC, so I cloned the public source to read it.

Labels used below. **[PRINTED]** means a value legible in the article. **[DERIVED]** means it follows from printed values by circuit analysis, with the assumptions stated. **[UNKNOWN]** means it is not in any source, and any number used for it will be a labelled estimate.

The article does not say which Selmer model it describes. It calls the instrument "the Clavioline", built under licence in the UK. Any "Concert"-specific differences would need another source.

### Sources added after Stage 1 (secondary, used to fill gaps only)

4. **Gordon Reid, "The Story Of The Clavioline", *Sound On Sound*, March 2007** (https://www.soundonsound.com/reviews/story-clavioline). A history piece by an owner of a Selmer Concert. It gives no circuit values. Facts used:
   - 36 keys, three octaves **from a low F to a high E**, high-note priority, ±1 octave transposition, five octaves in all. **This settles U10** (the 1952 article's "36 sets of key contacts and resistors" agrees).
   - The raw tone is a "harmonically rich, buzzy waveform similar to a square wave". This supports a **50 % duty** default for U5.
   - Three vibrato speeds (I, II, III) and two depths (the Amplitude tablet), true frequency modulation. No Hz values: the 4.5 / 5.5 / 6.5 Hz still come only from the 1952 article.
   - The knee lever: at rest almost no sound; it articulates swells, staccato and plucks.
   - The Selmer service manual is quoted: the amplifier is built so that "a large amount of distortion is deliberately obtained".
   - A longer voicing list (37 patches, merged from Selmer tables) that agrees with the 1952 Table 1 where they overlap, **including Cornet = tablet 6 alone** (so U3 is not a misprint in one table).
   - **Conflicts and additions for the Concert model:**
     - SOS says three octave dividers derive all pitches from a single top-octave tone generator. The 1952 article (and a 1953 Gibson Clavioline restorer, who describes a precision resistor chain behind the keys) describes a full 36-step resistor ladder on one multivibrator with no dividers.
     - The Concert has **22 tablets**: the 18 plus **Sub I** (one octave down) and **Sub II** (two octaves down), each with an "f" (forte) setting, mixed with the main tone.
     - So the Concert's tone generator differs from the circuit in the 1952 article. A Selmer "Clavioline Concert tone generator" schematic is listed online (el34world.com) but has not been read yet.
6. **Selmer Electronics, "Tone Generator — Concert Clavioline", drawing DRG No. 1289** (el34world.com, `Selmer_clavioline_concert_tonegenerator.pdf`, 281 KB; a faint scan). Facts read from it:
   - Valves: V1–V3 12AU7, V4 6BA6, V5–V7 12AU7.
   - **The Concert keeps the full key resistor ladder** (resistors R260–R311 to the −30 V line) on a single oscillator, as in the 1952 article.
   - Its sub-octaves come from **two extra divider stages (V5, V6, cross-coupled 12AU7 flip-flops, triggered through 75 pF)** and a mixer (V7), switched by four tablets: **I p, II p, I f, II f**.
   - So SOS's "three octave dividers from a top-octave generator" does not describe the Concert's main pitch chain. The dividers exist only for Sub I / Sub II. **This settles the oscillator question in U10.**
   - The control valve's anode load is **100 kΩ** and its coupling to the filter is **0.02 µF** (U2, partly). The keyboard HT is **150 V**.
   - The filter section matches Fig. 4 tablet for tablet (1–9, O, A, B, V, the 100 pF output capacitor, 0.001 µF, 20 kΩ + 0.05 µF, the "special" R32). The scan is too faint to read most values, and **no inductances are marked**.
   - Muting switches are ganged on the divider stage; their exact function was not determined.
5. **antiqueradio.org, Gibson Clavioline (1953) restoration** (https://www.antiqueradio.org/GibsonClaviolineKeyboardInstrument.htm). It confirms the two key contacts closing with "a very slight delay" for a click-free attack, the resistor-chain keyboard, and the knee lever as a carbon-track potentiometer. It gives no tube types or filter values.

---

## 1. The original synthesis architecture

```
 keys ──► V1 6SN7 multivibrator ──► V2 6BA6 control valve ──► Fig.4 tone-forming ──► Fig.5 amp ──► 10" speaker
         (R ladder, 36 taps;          (remote-cut-off pentode,     network (13 tablets,      (6J5 + 2×6V6,
          S1 coupler caps)             gated by key contact 2,      R32 knee lever across     "controlled",
             ▲                         RC attack, tablet P)         its input)                mostly 2nd harm.)
             │ cathode
         V3 6SN7 vibrato osc ─► V4 L63 follower (S3/S4/S5 rate, S6 depth)
```

### 1.1 Oscillator (Fig. 2, V1)
- This is a 6SN7 multivibrator. The patent describes it as using only resistors plus **one** coupling capacitor (anode 1 to grid 2), with frequency set by "the time constant of the discharge circuit".
- **Pitch:** each key grounds one junction of the series ladder R39, R40, R41 … (36 sets of contacts and resistors, tolerance ½ %). Each resistor step lowers the pitch by one semitone.
- **Highest-note priority is a physical consequence of the ladder** [DERIVED]. The grounded tap nearest the grid shorts out everything below it, so the highest key always wins. The article confirms this: "only the higher of the two notes will sound."
- **Coupler S1 (Low / Med / High)** switches C6–C10 (2300 pF, 4400 pF, 2100 pF and trimmers) in or out, transposing by an octave. With three octaves of keys, that gives five octaves in total. C11 (750 pF) and C12 (250 pF) are "final value selected at factory".
- **At rest the oscillator is stopped** by 30 V of bias through the ladder. Pressing a key removes that bias. This means **every note after silence starts from the same oscillator state** [DERIVED].
- **Waveform:** "the output from the control valve is trapezoidal". The duty cycle, edge time and edge curvature are **[UNKNOWN]**.
- **Tuning:** VR6 (performer tuning). The rest is factory preset.

### 1.2 Gate and attack (Fig. 2, V2, cathode network)
- Each key has two contacts. Contact 1 starts the oscillator. Contact 2, a moment later, grounds the V2 cathode network through R1 (1 kΩ). Every key's contact 2 is wired to the same bus. [DERIVED] consequences:
  - **Legato does not retrigger.** While any key is held the gate stays open, and only the pitch jumps.
  - On release, the gate closes before the oscillator stops.
- V2's cathode network (C1 0.1 µF, C2 0.1 µF, C3 0.25 µF, R3 50 kΩ, R4 2.5 kΩ, R7 5 kΩ, R8 500 kΩ to HT, R2 500 kΩ preset) sets the attack. Tablet **P** (S2) shorts R3 + R4.
  - Normal attack, discharge time constant: **≈ 18–20 ms** [DERIVED, Elmore estimate: ≈ R3·(C2+C3) + (R4+R7)·C3].
  - With P: **≈ 1.5–1.7 ms** [DERIVED: (R7+R1)·C3]. The article says P makes "a definite transient … at the commencement of each note".
  - These figures ignore V2's own cathode current, so they are first-order only.
- **V2 is a 6BA6, a remote-cut-off (variable-µ) pentode.** Its gain changes smoothly with bias, so it works as a valve VCA. The audible attack is therefore a delay followed by a smooth, roughly exponential-in-dB swell. It is not a linear ramp. The delay depends on the quiescent cathode voltage, which is set by preset R2 and the HT voltage. **Both are [UNKNOWN].**
- **Release** [DERIVED, qualitative]: the cathode recharges through R8 (500 kΩ) from HT, and V2 cuts off once the cathode rises a few volts. That should take only a few milliseconds. The exact shape is **[UNKNOWN]**.

### 1.3 Vibrato (Fig. 3, V3/V4)
- V3 (6SN7) is an RC oscillator. Tablets S3, S4 and S5 select **4.5, 5.5 and 6.5 Hz** [PRINTED] by switching parts of the R13–R18 ladder. With none pressed, the vibrato is off ("normally inoperative").
- **Depth (tablet S6, "AMP"):** S6 shorts R23 (1.5 MΩ) in a divider with R24 (500 kΩ). Open, the ratio is 0.25. Closed, it is ≈ 0.96 (R20, 23 kΩ, is the source). So **the two depths differ by about 3.8:1 (≈ 11.6 dB)** [DERIVED]. The absolute depth in cents is **[UNKNOWN]**: it depends on VR5 and V1's cathode sensitivity.
- The vibrato reaches V1 through the shared cathode resistor. In the article's words, it gives mainly frequency shift with "a small amount of amplitude modulation" [PRINTED].
- **[UNKNOWN]:** the vibrato waveform (the article does not say; it is probably near-sinusoidal for this oscillator type), and what happens when two speed tablets are pressed together.

### 1.4 Expression (knee lever)
- **R32**: "a variable resistor … of special construction and grading, connected across the output from the control valve". On the Fig. 4 schematic it is a **shunt across the network input node**.
- Because V2 is a pentode, roughly a current source, R32 does two things. It sets the level, and it **damps every resonance on that node**, so **expression also changes the timbre**. The patent says this too: "control the volume and possibly the timbre".
- **[UNKNOWN]:** the resistance range of R32 and its taper.

### 1.5 Amplifier and loudspeaker (Fig. 5)
- The amplifier is a 6J5 followed by two 6V6, with an output transformer. It is "designed to introduce a controlled amount of harmonic distortion", and the speaker "has been specially produced to add its measure"; "most of the distortion … is 2nd harmonic" [PRINTED].
- **No component values are printed in Fig. 5**, and the speaker response is **[UNKNOWN]**.
- The distortion comes after R32, so **playing louder adds more 2nd harmonic** [DERIVED].

### 1.6 Tablets
There are 18 tablets [PRINTED]: tone 1–9, O, A, B and V (13), plus P (attack), VIB 1/2/3 and AMP (vibrato depth). The coupler is a separate lever. Table 1 lists stop combinations for 15 instruments. Section 4.4 reproduces it.

---

## 2. Figure 4, switch by switch

### 2.1 How I traced it
I traced every wire in both your enlargement and the article's own scan (at 500 dpi), and checked every junction dot, gap and crossing without a dot. Conventions in the figure:
- Junctions are drawn with dots. Crossings without dots are not connected.
- The heavy bar above L1–L4 is a **common magnetic core**, so the four are windings of one transformer. The patent's Fig. 3 uses the same idea.
- L5 has no core bar.

### 2.2 Nodes

| Node | What joins there |
|---|---|
| **IN** ("From V2") | L4 (one end), the 7/8/9 return rail, C30, C17, C28, R31, R32 |
| **OUT** ("To Amp") | L5 (top), the common top of C24/C25/C26, R28, C30 |
| **X** | L4 (other end) to L2. Nothing else: **L4 and L2 are in series** |
| **NB** | L2 to L1, C27 (to switch A), switch B |
| **N1** | L1 (other end), R26 ∥ C20 (to switch 3), R27 ∥ C21 (to switch 4) |
| **N3** | L3 (the other end of L3 is grounded): C22 (to switch 5), C23 (to switch 6) |
| **NL5** | L5 (bottom), C18 (to switch 1), C19 (to switch 2), the return of switch O |
| **NV** | C17 → R29 to NV; R30 from NV to ground; switch V |

So the transformer is a **tapped winding IN → (L4+L2) → NB → L1 → N1**, plus an **isolated secondary L3** that is grounded at one end.

### 2.3 Netlist (as drawn; all tablets shown open)

```
* fixed parts
C30   IN  OUT  100pF          [PRINTED]
C17   IN  a    100pF          [PRINTED, label placement: medium confidence]
R29   a   NV   5k             [PRINTED]
R30   NV  0    3M             [PRINTED]
C28   IN  0    0.001uF        [PRINTED]
R31   IN  b    20k            [PRINTED]
C29   b   0    0.05uF         [PRINTED]
R32   IN  0    (variable)     [UNKNOWN range/taper]  knee lever
L4    IN  X    ?              [UNKNOWN]  } one core:
L2    X   NB   ?              [UNKNOWN]  }  mutual coupling,
L1    NB  N1   ?              [UNKNOWN]  }  turns ratios and
L3    0   N3   ?              [UNKNOWN]  }  winding sense unknown
L5    OUT NL5  ?              [UNKNOWN]
* tablets (closed = connected)
S1:  C18 NL5-0 0.02uF  | S2: C19 NL5-0 0.005uF
S3:  (R26 250k ∥ C20 0.005uF) N1-0
S4:  (R27 250k ∥ C21 0.015uF) N1-0
S5:  C22 N3-0 0.01uF   | S6: C23 N3-0 0.005uF  (printed "005µF", read as 0.005)
S7:  C24 IN-OUT 200pF  | S8: C25 IN-OUT 0.005uF | S9: C26 IN-OUT 0.015uF
SO:  R28 OUT-NL5 10k   (in parallel with L5)
SA:  C27 NB-0 0.02uF
SB:  NB-0 short
SV:  NV-0 short        (puts C17 + R29 across IN)
* outside Fig. 4
source: V2 anode (6BA6) with load R5, through C5   [R5, C5, ra UNKNOWN]
load:   amplifier input V5 grid, R33               [UNKNOWN]
```

### 2.4 The structure: three sub-networks sharing one node

```
          I_V2 (≈ current source, trapezoid)
            │
   IN ──────┼──────────┬───────────┬──────────────┬───────────────┐
   │        │          │           │              │               │
 R5‖ra   R32 knee   R31+C29     V branch     transformer     Cs = C30 (+C24/25/26 by 7/8/9)
 [UNK]   [UNK]      C28 fixed   (C17+R29)    (A,B,3,4,5,6)        │
                                                                 OUT ── R_amp [UNK]
                                                                  │
                                                                 L5 (‖ R28 if O)
                                                                  │
                                                                NL5 ── C18/C19 (1/2) ── 0
```

The output voltage is **V_OUT = I_V2 × Z_IN × H_OUT**, where:
- **H_OUT = V_OUT/V_IN** depends only on tablets 1, 2, 7, 8, 9 and O, plus the amplifier input impedance. It is **independent of the transformer and of R32**, so it factors out exactly.
- **Z_IN** is the parallel combination of everything on node IN. That includes the output branch itself, so every tablet affects Z_IN. This shared node is **why the tablets interact and do not behave as separate EQs**.

### 2.5 Each tablet

Resonance ratios below are **independent of the unknown inductances**, so they are exact for the topology [DERIVED]. Absolute frequencies are not.

**Output branch: tablets 1, 2, 7, 8, 9, O (the resonant high-pass)**
- **7, 8, 9** add C24 (200 pF), C25 (5 nF) and C26 (15 nF) in parallel with the always-present C30 (100 pF). They form one series capacitor **Cs** between IN and OUT, which takes eight values from 100 pF to 20.3 nF. With 1 and 2 both off, Cs feeds only the amplifier input resistance, giving a first-order high-pass. Larger Cs means more bass and body. The corner frequency depends on R_amp **[UNKNOWN]**.
- **1** (C18, 20 nF) and **2** (C19, 5 nF) complete the branch OUT → L5 → C_sh → ground. Ignoring loading:
  `H_OUT = (1 − ω²L5·C_sh) / ((1 + C_sh/Cs) − ω²L5·C_sh)`
  - There is a **notch at f_z = 1/(2π√(L5·C_sh))**.
  - There is a **resonant peak at f_p = f_z·√(1 + C_sh/Cs)**.
  - The low-frequency floor is **Cs/(Cs + C_sh)** and the gain above the peak is 1. The result is an **elliptic-style resonant high-pass**, a formant above a notch.
  - 2 alone puts the notch **exactly one octave above** 1 alone (ratio √(20/5) = 2). 1 and 2 together put it **1.9 semitones below** 1 alone.
- **The peak position depends on 7/8/9:**

| Cs (tablets) | f_p/f_z with 1 | LF floor with 1 | f_p/f_z with 2 | LF floor with 2 |
|---|---|---|---|---|
| 100 pF (none) | 14.2 | −46 dB | 7.1 | −34 dB |
| 300 pF (7) | 8.2 | −37 dB | 4.2 | −25 dB |
| 5.1 nF (8) | 2.2 | −14 dB | 1.4 | −6 dB |
| 15.1 nF (9) | 1.5 | −7 dB | 1.15 | −2.6 dB |

- **O** puts R28 (10 kΩ) across L5. This lowers the Q of the 1/2 resonance, so it has an **audible effect only when 1 or 2 is on** (with neither, NL5 floats). Table 1 uses O only together with 1 (Violin, Viola, Piccolo). That is consistent.

**Transformer branch: tablets A, B, 3, 4, 5, 6**
- Current flows in the windings **only if a path to ground exists at NB (A or B) or at N1 (3 or 4)**. Without one, the chain is a dangling winding and draws no current [DERIVED].
- **3** (C20, 5 nF ∥ 250 kΩ) and **4** (C21, 15 nF ∥ 250 kΩ) put a capacitor at the end of the whole chain (L4+L2+L1). This is a **series resonance to ground (a notch in V_IN)**, with a parallel resonance above it against the node capacitance (C28, Cs and strays): a notch and a formant together. Ratio f(3)/f(4) = √3 (**9.5 semitones**); 3 and 4 together = 20 nF. The 250 kΩ resistors are most likely bleeders that keep the capacitors discharged so that switching does not click. They also limit Q slightly.
- **A** (C27, 20 nF at the tap NB) makes a series resonance with only the L4+L2 part of the winding, plus the L1 path if 3 or 4 is on.
- **B** shorts the tap NB to ground:
  - L4+L2 becomes an inductive shunt across IN, which acts as a high-pass on V_IN.
  - L1 plus whatever 3 or 4 adds becomes a closed tank loop, which couples magnetically back into the primary.
  - **With B on, A does nothing**, because C27 is shorted.
  - Table 1: Banjo = 3 4 B P, Musical Saw = 3 B.
- **5** (C22, 10 nF) and **6** (C23, 5 nF) tune the **secondary L3**. A tuned secondary reflects into the primary as a **coupled resonator**: a split pair of resonances whose spacing depends on the coupling coefficient [UNKNOWN]. Ratio f(6)/f(5) = √2 (**6 semitones**); 5 and 6 together = 15 nF. **By the drawing, 5 and 6 do nothing unless A, B, 3 or 4 is also on.**

**Input shunt: tablet V**
- **V** grounds NV, putting **C17 (100 pF) + R29 (5 kΩ)** across IN: a shelving high-frequency cut. When V is open, R30 (3 MΩ) makes it inert (and keeps C17 discharged). How strong the cut is depends on the source impedance at IN [UNKNOWN]. Table 1 offers "O or V" as alternative ways of softening the Violin.

**Fixed parts** (no tablet)
- R31 + C29 (20 kΩ + 50 nF): a ≈ 20 kΩ load on IN above ≈ 160 Hz. This is probably the main damping of every IN-node resonance.
- C28 (1 nF): a low-pass on IN, near 8 kHz if the IN node sits at ≈ 20 kΩ [DERIVED, conditional].
- C30 (100 pF): the always-present path to the output.

### 2.6 Uncertainties and open questions

| # | Issue | Effect |
|---|---|---|
| U1 | **L1–L5 inductances, coupling coefficients, turns ratios and winding sense are not printed** | All absolute filter frequencies and Qs. **This is the biggest gap.** |
| U2 | Source impedance (R5, 6BA6 ra, C5) and amplifier input (R33, V5 input capacitance) not given | Overall Q, bass corner, and the strength of V and 7/8/9 |
| U3 | **Table 1: Cornet = tablet 6 alone.** By the drawing, 6 is inert without A, B, 3 or 4 | Either something is not shown (winding capacitance, a normally-closed contact such as B, a production change), or the table or drawing has an error. **This is the only inconsistency I found.** |
| U4 | R32 range and taper ("special construction and grading") | How expression feels, and how much it changes timbre |
| U5 | Trapezoid duty cycle and edge time; whether edge time is fixed in seconds (which would make high notes duller) | Harmonic content before the filter |
| U6 | V2 bias point (R2 preset, HT), 6BA6 curve, release shape | Attack delay and curve; only the time constants are derivable |
| U7 | Vibrato depth in cents, waveform, behaviour of combined speed tablets | Vibrato character |
| U8 | Fig. 5 values and speaker response | Amount and spectrum of the distortion |
| U9 | Small label readings: C17 = 100 pF (label placement), C23 printed "005µF" (read as 0.005 µF), C30 read as "C30" | Minor |
| U10 | ~~Keyboard compass~~ **Settled: 36 keys, F to E** (SOS + the 1952 article). Still open: the absolute pitch of the low F, and **the Concert's divider tone generator and Sub I/II** (see the sources) | Key range mapping; whether the Concert needs a different oscillator |
| U11 | **The V2 bias network's DC levels do not add up from the printed values**: with R8 = 500 kΩ from HT and ≈ 58.5 kΩ to ground at key-down (P off), the cathode would sit around 10 % of HT, too far into cut-off to conduct. Only the time constants are used; levels are normalised | The attack's delay and curve shape (time constants unaffected) |

**What can be modelled faithfully:**
- the topology of Fig. 4 and every tablet's connection;
- all printed capacitor and resistor values;
- the resonance ratios above;
- the H_OUT factorisation;
- highest-note priority, legato without retrigger, and oscillator restart after silence;
- the attack time constants and P;
- the three vibrato rates and the depth ratio;
- R32's position, and therefore the coupling between expression and timbre;
- 2nd-harmonic distortion placed after expression.

**What needs labelled estimates:** U1, U2 and U4–U8. They will live in a single parameter table marked ESTIMATE, voiced against Table 1's instrument targets and, ideally, against recordings of a real instrument.

---

## 3. Proposed digital implementation for the FM-1

Constraints (from Dinghy's rules): integers only, no FPU, no 64-bit division, a 256-frame block at 44.1 kHz (5.8 ms, target under 2.9 ms), and no flash access or drawing inside the audio ISR.

### 3.1 Signal chain (per sample, mono)

```
note stack → pitch (coupler, tune, vibrato FM) → band-limited trapezoid (+ small vibrato AM)
 → V2 gate gain (remote-cut-off curve of an RC cathode voltage) → current into the Fig.4 network model
 → V_OUT → valve stage (asymmetric 2nd-harmonic shaper, DC blocker) → [optional speaker EQ] → out
```

1. **Voice and keys:** a 27-key note stack plus MIDI notes, with highest-note priority.
   - Pitch changes while any key is held never retrigger the gate.
   - After full silence, the oscillator phase resets, as the stopped multivibrator would.
   - Pitch = 32-bit phase increment from a 12-TET table, ×2 per coupler step, plus a tune trim (VR6) and vibrato.
2. **Trapezoid:** a naive trapezoid with **polyBLAMP** correction at its four corners (it has slope discontinuities, not steps). This avoids aliasing at roughly 30–40 multiplies per sample.
   - Parameters: duty D and edge time t_e. Both are ESTIMATES (U5), voiced by ear.
   - Edge time can be set in seconds, so that high notes get relatively softer edges, as an RC-limited valve circuit would produce. This is an option, not a documented fact.
3. **Gate (V2):** a two-pole RC model of the cathode network, with P switching the resistances, gives the cathode voltage v_k(t). Gain is g = exp(−v_k/V0), a remote-cut-off approximation that runs from a table at the control rate (once per 16 samples).
   - The time constants come from the printed values: about 19 ms normal, about 1.6 ms with P.
   - The quiescent voltage and V0 are ESTIMATES (U6) that set the delay and swell.
4. **Tone-forming network (the core):** see 3.2.
5. **Valve and speaker:** y = x + a·x·|x|-style asymmetric shaping (mostly even order), followed by a DC blocker. Placed after the network, it automatically gets stronger with expression. The amount is an ESTIMATE (U8). A speaker EQ is optional and labelled non-historical.
6. **Vibrato:** a sine LFO at 4.5, 5.5 or 6.5 Hz. Depth ratio 1 : 3.8 between AMP off and on (derived). The absolute depth in cents is an ESTIMATE (U7). A small AM component goes on the trapezoid level.

### 3.2 The network: recommended approach

**Reference model (on the PC, Stage 3):** a Python modified-nodal-analysis model of the exact netlist in 2.3, with all [UNKNOWN] parameters in one table. It computes the exact response for any tablet set and R32 setting. This is the yardstick every device version is compared against, and where voicing happens: Table 1 presets, plots, and recordings if we can get them.

**On the device: a linear state-space model (discretised nodal/DK method) of the same netlist.**
- The network is linear: R, C and coupled L only. Discretising it with the bilinear transform gives `x[n+1] = A·x[n] + B·u[n]`, `y = C·x + D·u`. This is **exact for the circuit and parameters it is given**, apart from frequency warping near Nyquist.
- **States:**
  - capacitors in parallel between the same nodes merge, so the tablets only change values: Cs (C30+7/8/9), C_sh (1/2) and C_N1 (3/4) are one state each;
  - the three transformer windings (L4+L2 as one winding, L1, L3);
  - L5, C27, C_N3 (5/6), C17, C28, C29, and the source/load parts.
  
  That is ≈ 12–14 states with everything on, and fewer in practice, because inactive branches are removed.
- **Coefficients are recompiled in the main loop**, not in the ISR, whenever a tablet or the knee lever (R32) changes. A 10–14-node matrix is inverted with a small software floating-point routine (mantissa/exponent in int32/int64, a reciprocal by Newton iteration, so no 64-bit division). That takes milliseconds at most, at a control rate of about 100 Hz for the lever.
  - The new coefficients are handed to the ISR with a double buffer and a short crossfade, so a tablet change does not click. Real tablets would click; we can make that optional.
- **ISR cost:** about 14×15 ≈ 210 32×32→64 multiply-accumulates per sample, ≈ 9.3 M per second. This **must be measured on the FM-1** (`fm1_ticks()`), because I do not have a reliable cycle count for pi32v2 multiply-accumulate.
- **Why this approach:**
  - The tablets interact through physics, not presets.
  - Expression changes timbre automatically.
  - Every printed value is used directly.
  - The unknowns stay as named, adjustable parameters rather than being buried in fitted coefficients.
  - The device model can be checked sample-for-sample against the PC reference.

**Fallbacks if the CPU or the fixed-point numerics fail:**
1. **Model-order reduction:** drop states whose poles lie far above the audio band (the 100 pF / 5 kΩ / 20 kΩ corners), and merge near-ideal transformer coupling (k → 1).
2. **Tables with factorisation:** H_OUT has only **56 distinct configurations** (tablets 1, 2, 7, 8, 9, O, with redundant ones removed). It can be stored exactly as biquads. The IN-node part (≈ 45 distinct transformer states × V) becomes a precomputed cascade of 4–5 resonators per configuration, at 2–3 R32 points, interpolated in pole-radius/frequency form.
   - This keeps the interaction but approximates expression's effect on timbre.
   - Flash cost: tens of kilobytes. The FM-1 app area is about 568 KB.

### 3.3 Accuracy statement (to repeat in the firmware docs)
The model is **topologically faithful and uses every printed value**. Absolute formant frequencies, Qs, waveform details, attack curvature, vibrato depth and distortion depend on undocumented parameters (U1–U8). They are **estimates voiced by ear**, not historical facts, and the firmware's parameter table will say so.

---

## 4. Physical control layout on the FM-1

FM-1 panel (Dinghy IDs):
- 27 keys F3..G5 (16 white, 11 black);
- buttons OCT−, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC;
- encoders SELECT, ALGORITHM, KNOB1–4, PRESETS (relative, with detents);
- **one absolute analogue pot: MASTER**;
- a 240×240 screen; LEDs on every key and button.

### 4.1 Knee lever = PRESETS encoder (decided after Stage 1)
- **MASTER stays the volume**, unchanged from Dinghy's platform. (Stage 1 proposed MASTER as the lever; you chose otherwise.)
- The knee lever is the **PRESETS encoder**: 64 steps, step 0 = silent ("at rest, almost no sound", Sound On Sound), then a dB-linear taper (ESTIMATE). It accelerates: one step per detent when turned slowly, ×2 under 70 ms between detents, ×4 under 30 ms, so a quick flick gives a swell or a pluck. The audio smooths it over ~12 ms, so the steps do not zipper.
- **MIDI CC 11 (expression) also moves it**, so an expression pedal through a MIDI interface on the TRS jack gets closest to a real knee lever.
- The lever is not saved to flash (it moves all the time). It powers up at step 48.

### 4.2 PLAY mode
| Control | Function |
|---|---|
| All 27 keys (+ MIDI in) | Monophonic, highest-note priority, legato without retrigger |
| PRESETS encoder | Knee lever (R32) |
| MASTER | Volume (platform) |
| OCT− / OCT+ | **Coupler** LOW / MED / HIGH (the historical S1). LEDs show the position |
| ALGORITHM encoder | Keyboard window +0 / +1 octave: the FM-1 has 27 keys against the original's 36 (F..E) (FM-1-only feature) |
| LFO | Vibrato speed: off → 1 (4.5) → 2 (5.5) → 3 (6.5) Hz |
| FX | Vibrato depth AMP − / + |
| ENV | Tablet P (attack) |
| SELECT encoder | Presets (Table 1 and the Sound On Sound voicing list, plus user slots); the name shows on screen |
| EDIT | Enter STOPS mode |
| SAVE | Store the current setup to a user slot (written only while silent, as Dinghy's rules require) |
| KNOB1 | Fine tune (VR6 equivalent: the performer's tuning control) |
| KNOB2–4 | Unused in PLAY, to keep it simple |

### 4.3 STOPS mode (EDIT toggles; HOME or EDIT returns, keeping everything)

```
 black:      F#3 G#3 A#3   C#4 D#4   F#4  G#4  A#4   C#5  D#5   F#5
                                     VIB1 VIB2 VIB3  AMP
 white:  F3  G3  A3  B3  C4  D4  E4  F4  G4  A4  B4  C5  D5  E5  F5  G5
          1   2   3   4   5   6   7   8   9   O   A   B   V   P   ·   ·
```

- 14 white keys hold the 13 tone tablets plus **P**, in schematic order. The two spare white keys (F5, G5) could be "clear all" and "recall preset" (optional).
- The three-black-key group F#4/G#4/A#4 = **VIB 1/2/3**; C#5 = **AMP** (vibrato depth). I read your "amplitude" as Table 1's AMP column (vibrato amplitude, tablet S6). If you meant P, it moves to D#5.
- Key LEDs show which tablets are down.
- The PLAY button holds a **drone of the last note** while you edit, so changes are heard immediately. MIDI notes also keep playing in STOPS mode.

### 4.4 Screen (inspired by the Clavioline front rail)
- **Top:** the coupler (LOW / MED / HIGH), and a vertical bar for the knee lever.
- **Middle:** a row of 18 cream tablets with black legends, `1 2 3 4 5 6 7 8 9 O A B V | P | 1 2 3 + |`. Pressed tablets are drawn "down" and lit amber. The same strip appears in both modes, and in STOPS mode the cursor and labels get larger.
- **Bottom:** the note playing, and the preset name ("VIOLIN" or "USER 3").
- Only changed regions are redrawn, as Dinghy requires.

**Table 1 presets** [PRINTED], with VIB = speed tablet, AMP = depth, and Coupler:

| Instrument | Stops | Letter | Vib | Amp | Coupler |
|---|---|---|---|---|---|
| Violin | 1 | O or V | 2 | + | High |
| Viola | 1 | O or V | 2 | + | Medium |
| Tenor Sax | 4 | – | 3 | – | Medium |
| Trumpet | – | – | 2 | – | Med.–High |
| Trombone | 3 | – | 2 | – | Low |
| Horn | 2 3 | – | 3 | + | Low |
| Bassoon | 3 7 | – | – | – | Low |
| Cornet | 6 | – | 1 | – | Medium |
| Oboe | 1 4 8 | – | 1 | – | Medium |
| Flute | 3 4 5 | – | 1 | – | High |
| Piccolo | 1 4 O | – | 2 | – | High |
| Hawaiian Guitar | 1 4 6 | P | 2 | + | Medium |
| Banjo | 3 4 | B P | – | – | Medium |
| Mandolin | 3 6 8 | P | – | – | High |
| Musical Saw | 3 | B | 2 | + | High |

Notes on transcribing the table:
- "Med.–High" is not one of the three coupler positions. It may mean "either", which is a choice for you.
- "O or V" will ship as two presets, *Violin (O)* and *Violin (V)*.
- I read "140" as 1, 4, O, because no tablet 0 exists.

---

## 5. Implementation plan

### Stage 2: first playable prototype
1. New repository **`clavioline-fm1`** (GPL-3.0, as Dinghy requires).
   - Copy Dinghy's platform (`platform.c`, `hal/`, `loader/`, `usb.c`, `ota.c`, `recovery.c`, `bootguard.h`, `tools/`, `tests/host.c`) unchanged, except for the MASTER hand-off.
   - Write a new `clav.c` app.
   - Pick a new, untaken `FM-1_9NN` identity and set `APP_NAME "CLAVIOLINE"`.
2. Write the note stack (highest priority, legato without retrigger, phase reset after silence), the polyBLAMP trapezoid, the coupler (OCT±), the RC attack plus remote-cut-off gate with P, and MASTER expression (a simple level control until the network exists).
3. Run host-mode tests that produce WAVs and screen PNGs:
   - highest-note and legato scripts;
   - an alias-level check on the trapezoid;
   - attack timing against 19 ms and 1.6 ms.
4. **Build `.fwsc`.** The JieLi toolchain must come from Linux, WSL or Docker on your Windows PC; this was *not* verified here. Then flash after a full backup (mvave-fm1-firmware skill) and test:
   - OCT− rescue;
   - the two-crash fallback;
   - installing another firmware over this one.
5. Measure ISR time on hardware.

### Stage 3: tone-forming network
1. **Python reference:** netlist → MNA → responses for all Table 1 presets and R32 sweeps, with plots. Create parameter set **v0** (all ESTIMATE). Voice it against Table 1's instrument names and recordings, if you can provide or point to any.
2. **Resolve U3 (Cornet).** Simulate both readings (as drawn, and B normally closed) and pick the one that makes Table 1 hang together. Document the choice.
3. **Device:** state-space compiler (main loop, soft-float) and fixed-point state update (ISR), with a crossfade on configuration change.
4. **Compare:** the device output must match the reference (spectral error within ±0.5 dB over 50 Hz – 12 kHz for every preset), checked by host tests.
5. Optimise: remove inactive states, use the reduced order, and use the fallback tables only if needed.

### Stage 4: authenticity and polish
- Vibrato (three rates, depth ratio 1 : 3.8, small AM), the valve 2nd-harmonic stage, optional speaker EQ.
- Full PLAY / STOPS / SETUP interface, the tablet-rail screen, the 15 Table 1 presets, and user slots.
- Tests:
  - CPU headroom under the worst case (all tablets on, vibrato, lever moving, MIDI flood);
  - fast tablet toggling while playing, checked for clicks;
  - long-run stability, and power cycling with settings saved;
  - USB audio capture to compare against the reference.
- Release with the `.fwsc`, source and SHA256SUMS, and **state plainly what was tested on hardware and which parameters are estimates**.

### Risks
1. **The unknown inductances (U1)** are the main threat to authenticity. Mitigations: recordings or measurements of a real unit, a service manual, or careful voicing guided by Table 1.
2. **CPU and fixed-point numerics** of the state-space model on a chip with no FPU: measure early in Stage 3, and keep the fallbacks ready.
3. **Toolchain access:** the JieLi hosts may be unreachable, and the skill notes that the build is unverified on this setup.
4. **Platform change to MASTER:** keep OTA and rescue exactly as in Dinghy and test them before any audio work.
5. **Licensing:** the whole firmware is GPL-3.0 because of the Dinghy and Felucca code.
