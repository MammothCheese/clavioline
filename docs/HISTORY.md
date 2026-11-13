# The Clavioline, and how this recreation was made

## The instrument

Constant Martin designed the Clavioline in Versailles in 1947. It was built under licence in France, the UK (by Selmer),
the USA (by Gibson), Germany and Italy. It is a small monophonic keyboard that clips under a piano keyboard:
- 36 keys, three octaves from F to E, with a slider transposing one octave up or down;
- a row of tablets (stop switches) along the front;
- a knee lever for expression;
- a separate cabinet holding the valve amplifier, the power supply and the loudspeaker.

The standard (Auditorium) model has 18 tablets:
- 13 tone tablets: 1–9, O, A, B, V;
- **P**, which sharpens the attack;
- three vibrato speeds, I, II and III;
- vibrato **Amplitude**.

The **Concert** model adds four more, **Sub I** and **Sub II** (one and two octaves down), each soft (**p**) or loud (**f**).

## Sources

In order of authority:

1. **G. H. Hillier, "The Clavioline", *Electronic Engineering*, October 1952, pp. 454–455.** The primary source:
   - full circuit diagrams of the tone generator (Fig. 2), the vibrato (Fig. 3), the tone-forming network (Fig. 4) and the amplifier (Fig. 5);
   - a description of how each part behaves;
   - a table of tablet settings for 15 instruments.
2. **Selmer Electronics, "Tone Generator — Concert Clavioline", drawing DRG 1289.** A faint scan of the Concert model's tone generator:
   - **the same resistor-ladder oscillator and tone network as 1952**;
   - the Sub I / Sub II divider valves and their four tablets;
   - the control valve's anode load (100 kΩ), its coupling capacitor (0.02 µF), and a 150 V supply;
   - no coil values.
3. **C. Martin, US patent 2,563,477 (filed 1948).** The principle:
   - a multivibrator whose pitch is set by one RC time constant;
   - a "coupling tube" held cut off until a key's second contact opens it after a delay, short for percussion or long for a gradual start;
   - a filter of windings on one core, in series between that tube and the amplifier, with capacitors switched to their ends and junction.

   The production circuit differs in detail, so the patent was used to confirm intent, never for values.
4. **Gordon Reid, "The Story Of The Clavioline", *Sound On Sound*, March 2007.** A history by the owner of a Selmer Concert:
   - the keyboard compass;
   - the raw tone ("harmonically rich, buzzy … similar to a square wave");
   - how the knee lever is played;
   - a quote from the Selmer service manual on the amplifier's deliberate distortion;
   - 37 Selmer voicings.
5. **A 1953 Gibson Clavioline restoration (antiqueradio.org).** It confirms the resistor chain behind the keys, the two key contacts closing with "a very slight delay" for a click-free start, and the knee lever as a carbon-track potentiometer.

## Reconstructing the circuit

### The tone generator (Fig. 2)

- **Pitch:** each key grounds one tap of a chain of ½ % resistors in the multivibrator's grid circuit.
  - The tap nearest the grid shorts everything below it, so **the highest key always wins**, and the article says so.
  - At rest a 30 V bias holds the oscillator stopped, so every note after silence starts from the same state.
- **Coupler:** switches the timing capacitors to drop or raise the pitch an octave.
- **Two contacts per key:** the first starts the oscillator; the second, an instant later, opens the **control valve**
  (a 6BA6) through its cathode network.
  - All keys share that second contact, so **sliding between held keys never restarts the attack**.
  - The cathode network's printed values give the attack time constants exactly: **19.2 ms**, and **1.54 ms with tablet P**.
- **The valve's turn-on:** the 6BA6 is a remote-cut-off valve, so it opens gradually, a soft swell rather than a switch.
  - The printed bias resistors do not give believable absolute voltages, so the swell's exact shape is an estimate. Its timing comes from the printed values.
- **Waveform:** the control valve's output is "trapezoidal in form": a square wave with sloped edges.
  - The Concert's sub-octaves are flip-flop dividers clocked by the oscillator, mixed in softly or loudly.

### The vibrato (Fig. 3)

A second 6SN7 oscillates at **4.5, 5.5 or 6.5 Hz** depending on the speed tablet. It reaches the main oscillator
through a shared cathode resistor, giving mostly pitch vibrato "with a small amount of amplitude modulation".

The Amplitude tablet shorts a 1.5 MΩ resistor in a divider with 500 kΩ, so **the deep vibrato is 3.8 times the shallow
one**. The depth in cents is not recorded.

### The tone-forming network (Fig. 4)

This is what makes a Clavioline sound like one. Every wire in Fig. 4 was traced, in an enlargement and in the
article's own scan at 500 dpi, with every junction dot checked.
- **L1–L4** are four windings on **one core** (the heavy bar above them).
- L4 and L2 are in series. Their junction with L1 is a tap, and **L3** is a separate secondary.
- **L5** is a separate coil.

The traced netlist, with every printed value:

```
source   control valve anode (100 kΩ load, Concert) --0.02 µF (Concert)-- IN
IN-OUT   C30 100 pF, always;  + C24 200 pF (7), C25 0.005 µF (8), C26 0.015 µF (9)
IN       C28 0.001 µF to ground;  R31 20 kΩ + C29 0.05 µF to ground;  R32 knee lever to ground
         C17 0.01 µF (see below) + R29 5 kΩ to ground when V is down (R30 3 MΩ otherwise)
IN-NB    L4 + L2              NB   C27 0.02 µF to ground (A);  short to ground (B)
NB-N1    L1                   N1   R26 250 kΩ ∥ C20 0.005 µF (3);  R27 250 kΩ ∥ C21 0.015 µF (4)
                                   C18 0.02 µF (1);  C19 0.005 µF (2)
N1-OUT   L5;  R28 10 kΩ across it (O)
L3       ground - N3;   N3   C22 0.01 µF (5);  C23 0.005 µF (6)
OUT      to the amplifier
```

So the signal runs **through** the coils (L4, L2, L1, L5) to the amplifier. The tablets hang capacitors and resistors
off that path, and C30 with tablets 7, 8 and 9 bridges round it. It is a resonant low-pass ladder with a bypass, as
in the patent: Martin's windings sit in series between valve and amplifier, with the junction grounded directly or
through a capacitor (here **B** and **A**).

Because everything shares one circuit, **the tablets interact**:
- **5** and **6** tune the secondary, and they matter only while current flows in the primary.
- **O** damps L5, softening tablets 1 and 2.
- **B** shorts the tap and disables **A**.
- The knee lever, a resistor across the input, changes the tone as well as the level. The patent says it controls "the volume and possibly the timbre".

#### One judgement call: the capacitor row

The drawing shows a small gap in the row of capacitors between C19 and R26. Taken literally, it cuts L5 off from the
coil chain:
- the coils become a dead-end branch;
- the only route to the amplifier is the 100 pF capacitor.

The first version of this firmware followed that reading. It sounded thin and harshly buzzy: the fundamental vanished,
and Selmer's own presets came out 46 dB apart in level.

Read as one continuous row, everything falls into place:
- the signal path matches the patent;
- the presets come out within a few decibels of each other, each with its own character;
- **Cornet, which Selmer's tables give as tablet 6 alone, finally does something.** With the gap, tablet 6 would have
  been inert, contradicting two independent Selmer tables.

The row is treated as one node.

#### Tablet V's capacitor

The 1952 drawing has a "100pF" label beside C17, but with 100 pF across a node of about 16 kΩ, V changes nothing
audible (0.1 dB). Selmer's Violin card offers "O or V" as alternatives, so on the real instrument V clearly did
something. On the Concert drawing, V's capacitor is faint: it could read ".1" or ".01".
- **0.1 µF** darkens the tone, but also drops the level 6–10 dB, so the V presets sound dull and quiet next to their
  O versions (O changes the level by only 0.8 dB).
- **0.01 µF** darkens the upper harmonics by 6–10 dB at the same level as O: a gentler alternative to O, as the card
  implies.

This firmware uses 0.01 µF.

#### Tablet A

A puts 0.02 µF from the coil tap (between L2 and L1) to ground. That makes a low-pass section in the middle of the
ladder, and with the estimated coils it changes some harmonics by up to 7 dB, as much as tablets 2 or 3. How strong it
really was depends on the coil values, which no document gives. None of Selmer's voicings use A, so nothing pins it
down further.

#### What is not in any document

The drawings give every capacitor and resistor but **no coil values**. Also missing:
- the amplifier's input impedance;
- the capacitance of the cable to the separate amplifier cabinet.

These were estimated from the printed capacitors, so that each tablet's resonance falls where an instrument maker
would put it (a few hundred hertz to a few kilohertz). [ENGINE.md](ENGINE.md) lists them. The patterns are the circuit's
own, whatever the coil values: tablet 2 tunes exactly an octave above tablet 1, tablets 3 and 4 are 9.5 semitones
apart, and tablets 5 and 6 are 6 semitones apart. The absolute frequencies are estimates.

### The amplifier (Fig. 5)

A 6J5 and two 6V6s, "designed to introduce a controlled amount of harmonic distortion", most of it 2nd harmonic. The
service manual says "a large amount of distortion is deliberately obtained". No values are printed, so the amount is an
estimate. It sits after the knee lever, so playing louder adds more distortion.

## From circuit simulation to a fitted model

The first versions simulated the whole Fig. 4 network on the FM-1, element by element, every sample. That proved the
reconstruction (the tablets interact as the drawing says), but it cost most of the FM-1's processor, and the result
still did not sound convincing. An audit against the article found the reasons elsewhere:

- **The wave shape:** a symmetric trapezoid has only odd harmonics. A valve multivibrator of this kind is uneven
  (one sharp edge, one recovering), which adds the even harmonics that make the tone reedy.
- **The amplifier and speaker:** the article and the service manual stress deliberate distortion and a speaker that
  adds its own; the engine had almost none (2nd harmonic 40 dB down) and no speaker at all.
- **The resonances:** with coil losses estimated at Q ~ 6, several tablets (A, 5, 6, 7, 9) changed little. Q ~ 15,
  usual for iron-core audio coils, makes them distinct.
- **Tablet P:** the article documents it as a shorter attack that makes "a definite transient". An earlier
  invented "ping" and an automatic decay were removed. A recording of a real Clavioline then showed the transient
  itself: each P note peaks and falls about 10 dB within 40 ms, then holds. The control valve's screen-grid capacitor
  (C4 on Fig. 2) accounts for it, and its dynamics are fitted to that recording ([ENGINE.md](ENGINE.md)).

- **Upper harmonics:** comparing a plain note (no tablets) with the same recording showed the firmware's C5 20–25 dB
  short from the 12th harmonic up. The cause was ours, not the instrument's: one wavetable per octave cut a note near
  the bottom of an octave at about 5.5 kHz. A table per half-octave keeps every note's harmonics to at least
  7.4 kHz, and C5 now sits within a few decibels of the recording there. Other differences in that comparison (a
  weak fundamental at C5, dips near 500 / 1500 / 2500 Hz) match the room's echo and were left alone.

The network is now solved on a computer for all 8192 tablet combinations, from the same model, and each response is
stored as five resonant filters. The FM-1 runs those instead of the circuit: the tablets still interact exactly as the
model says, at a fraction of the cost.

## Open questions

- The coil values and losses, and so the exact formant frequencies and strengths. Measurements or recordings of a
  surviving instrument would settle them.
- The knee lever's resistance range and taper ("special construction and grading").
- The vibrato's depth in cents, and what happens when two speed tablets are down together.
- The oscillator's exact wave shape (duty cycle, recovery), and the shape of the valve's turn-on.
- The function of the Concert's "muting" switches on its divider stage.
