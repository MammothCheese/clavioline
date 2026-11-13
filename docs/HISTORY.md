# The Clavioline, and how this recreation was made

## The instrument

Constant Martin designed the Clavioline in Versailles in 1947. It was built under licence in France, in Britain by
Selmer, in the USA by Gibson, and in Germany and Italy. In the 1950s and 60s it turned up everywhere from dance bands
to pop records: the lead line of the Tornados' "Telstar" (produced by Joe Meek) is a Clavioline.

It is a small keyboard, three octaves from F to E, that clips under a piano keyboard. It plays one note at a time.
Its amplifier, power supply and loudspeaker sit in a separate cabinet. The player has:
- a slider that moves the whole keyboard an octave up or down;
- a row of **tablets** (stop switches) along the front that set the tone colour;
- a **knee lever** under the keyboard for swelling the volume.

The standard model has 18 tablets: 13 tone tablets (1–9, O, A, B, V), **P** for a sharp attack, three vibrato speeds
(I, II, III) and vibrato **Amplitude** (depth). The **Concert** model, which this firmware follows, adds four more:
**Sub I** and **Sub II**, one and two octaves below the note, each soft (**p**) or loud (**f**).

## What there was to go on

No Clavioline was available to measure, so everything comes from documents and one recording. In order of authority:

1. **G. H. Hillier, "The Clavioline", *Electronic Engineering*, October 1952, pp. 454–455.** The main source. It
   prints the full circuit diagrams (the tone generator, the vibrato, the tone-forming network and the amplifier),
   explains how each part behaves, and gives the tablet settings for 15 instruments.
2. **Selmer Electronics, "Tone Generator — Concert Clavioline", drawing DRG 1289.** A faint factory drawing of the
   Concert model. It shows the same oscillator and tone network as the 1952 article, plus the sub-octave circuit and a
   few component values the article leaves out.
3. **C. Martin, US patent 2,563,477 (filed 1948).** The inventor's own description of the principle. The production
   instrument differs in detail, so the patent was used to confirm what the circuit was meant to do, never for values.
4. **Gordon Reid, "The Story Of The Clavioline", *Sound On Sound*, March 2007.** A history by the owner of a Selmer
   Concert. It describes the raw tone ("harmonically rich, buzzy … similar to a square wave") and how the knee lever is
   played, quotes the Selmer service manual on the amplifier's deliberate distortion, and lists 37 Selmer voicings.
5. **A 1953 Gibson Clavioline restoration (antiqueradio.org).** Practical details from someone who had one apart: the
   resistor chain behind the keys, the two key contacts, the knee lever's construction.
6. **Vintage Keys Studio, ["The Joe Meek Telstar Keyboard: The Clavioline"](https://www.youtube.com/watch?v=JK0m1moewgA)
   (YouTube).** A working Clavioline demonstrated note by note.

## How the original works

### Making a note

Behind the keys is a chain of precision resistors. Pressing a key connects one point on that chain to the oscillator,
which sets its pitch. The point nearest the oscillator cuts off everything beyond it, so when several keys are held,
**the highest one always sounds**.

Each key has **two contacts**. The first starts the oscillator. The second, a moment later, opens a **control valve**
that lets the sound through, so notes start cleanly without a click. All the keys share that second contact, so
sliding from one held key to the next changes the pitch without starting a new note.

The oscillator's output is described as "trapezoidal in form": roughly a square wave with sloping edges. That is the
bright, buzzy raw tone that everything else shapes.

### The vibrato

A second, slow oscillator wobbles the pitch at 4.5, 5.5 or 6.5 times a second, depending on the speed tablet, with "a
small amount of amplitude modulation". The Amplitude tablet changes a resistor so the deep vibrato is 3.8 times the
shallow one. The depth itself (how far the pitch moves) is not recorded anywhere.

### The tablets and the tone-forming network

This network is what makes a Clavioline sound like one, and it was traced wire by wire from enlargements of the
article's diagram, with every junction checked.

Between the control valve and the amplifier, the signal passes **through a chain of coils**. Four of them are windings
on one shared iron core; a fifth is separate. The tone tablets hang capacitors and resistors off this chain, and a
small capacitor (with tablets 7, 8 and 9 adding more) lets some of the signal bypass the coils altogether. Each
capacitor resonates with the coils at some frequency, emphasising part of the tone the way the body of an acoustic
instrument does.

Because it is one connected circuit, the tablets **interact**. A few examples:
- tablets 5 and 6 sit on a separate winding of the shared core, so they only have an effect when current flows in the
  main windings;
- tablet O damps the last coil, which softens what tablets 1 and 2 do;
- tablet B shorts the middle of the chain, which disables tablet A;
- the knee lever is a variable resistor across the network's input, so it changes the tone slightly as well as the
  volume. The patent says it controls "the volume and possibly the timbre".

This matches the patent: Martin's coils sit in series between the valve and the amplifier, with capacitors switched
onto their ends and their middle.

### The amplifier and speaker

The amplifier was "designed to introduce a controlled amount of harmonic distortion", and the service manual says "a
large amount of distortion is deliberately obtained". The speaker, too, was chosen to "add its measure". No values are
printed for either, so both are estimates.

## Judgement calls

Where the drawings were unclear, the choice was made by asking which reading made the instrument behave as its
documents say it did.

**The capacitor row.** The diagram shows a tiny gap in one row of capacitors. Taken literally, it disconnects the last
coil from the rest, leaving only the small bypass capacitor to carry the sound. The first version of the firmware
followed that reading, and it sounded thin and harsh: the presets came out wildly different in volume, and Cornet
(tablet 6 alone, in two separate Selmer tables) did nothing at all. Read as one continuous row, the signal path matches
the patent, the presets come out at similar volumes with their own characters, and Cornet works. The gap was treated as
a drawing artefact.

**Tablet V's capacitor.** The 1952 drawing labels it 100 pF, but that value would make V do almost nothing, while
Selmer's Violin card offers "O or V" as alternatives, so V clearly did something. On the Concert drawing the value is
faint and could read 0.1 or 0.01 µF. With 0.1 µF the V presets come out much quieter than their O versions; with
0.01 µF V darkens the tone at the same volume, a gentler alternative to O, as the card implies. This firmware uses
0.01 µF.

**Tablet A.** A puts a capacitor from the middle of the coil chain to ground. How strong its effect was depends on the
coil values, which no document gives, and none of Selmer's voicings use it, so nothing pins it down further.

## What had to be estimated

The drawings give every capacitor and resistor, but **no coil values**, and nothing on the amplifier's input or the
cable to the amplifier cabinet. The coils were estimated so that each tablet's resonance falls where an instrument maker
would put it, between a few hundred hertz and a few kilohertz. The relationships between tablets come from the circuit
itself whatever the coil values: tablet 2 resonates exactly an octave above tablet 1, tablets 3 and 4 are 9.5
semitones apart, and tablets 5 and 6 are 6 semitones apart. Only the absolute frequencies are guesses.

Also estimated: the oscillator's exact wave shape, how the control valve's opening curves, the vibrato depth, the knee
lever's taper, the amount of distortion and the speaker's response. [ENGINE.md](ENGINE.md) lists every estimate and
its value.

## Making the firmware

### First attempt: simulating the circuit

The first versions ran the whole tone network on the FM-1 as a live circuit simulation, every component, every sample.
That proved the reading of the circuit was right: the tablets interacted as the drawing says, but it used most of the
FM-1's small processor.

### Making it fit

Instead of simulating the network live, it is now solved in advance on a PC, from the same model, for all 8,192
combinations of the 13 tone tablets. Each result is stored as a set of five compact filters. The FM-1 just loads the
filters for whichever tablets are down. The tablets still interact exactly as the model says, at a small fraction of
the processing cost.

## Listening against a real Clavioline

Once the firmware sounded right in general, the Vintage Keys Studio video gave something real to compare against.

**Tablet P.** In the recording, each P note rises in a few milliseconds to a sharp peak, falls about 10 dB within 40
ms, then holds steady, and every note of a quick run peaks again. The firmware's P, which only shortened the attack,
had no peak at all. The circuit explained it: the control valve has a capacitor on its screen grid (C4 on the 1952
diagram) that charges up while the valve is shut. When a note opens the valve, that stored charge briefly gives it
extra gain, which sags away as the capacitor drains. A fast opening (P) turns that into a sharp peak; the normal slow
opening into a gentle swell, which matches the recording's sustained notes too. The capacitor's timings were fitted to
the recording's run of P notes, and the firmware now falls 8.3 and 10.2 dB at 20 and 40 ms against the recording's 7.6
and 10.0 dB.

**The plain tone.** Comparing a plain note with no tablets showed the firmware broadly right, with one fault: its
high notes had too little of their upper harmonics. That was an error in how the firmware stored its waveforms, not
anything about the instrument, and it was fixed. The recording also shows dips at regular frequencies that come from
the room's echo; those were deliberately not copied. Whether the original's low notes are as rich as they sound in the
recording could not be settled from one microphone in one room.

## Open questions

Measurements or clean recordings of a surviving instrument would settle these:
- the coil values and losses, and so the exact frequencies and strengths of the tablets' resonances;
- the knee lever's resistance range and taper ("special construction and grading");
- the vibrato's depth, and what happens when two speed tablets are down together;
- the oscillator's exact wave shape, and how the control valve's opening curves;
- what the Concert's "muting" switches on its sub-octave stage did.
L3       ground - N3;   N3   C22 0.01 µF (5);  C23 0.005 µF (6)
OUT      to the amplifier
```

The estimated values (coils, amplifier input, cable) are in [ENGINE.md](ENGINE.md), and the model itself is
`tools/clav_net_ref.py`.
