# CLAVIOLINE for the M-VAVE FM-1

<img src="docs/screen.png" width="240" align="right" alt="The FM-1 screen: the tablet rail, the preset, the note, the coupler and the knee lever">

Custom firmware that turns the FM-1 into a **Clavioline**, the valve keyboard Constant Martin invented in 1947 and
Selmer built in England in the 1950s. It is the reedy, vibrato-laden lead voice of records like "Telstar".

This is not a synthesizer "inspired by" the Clavioline. It rebuilds the original circuit:
- the oscillator retuned by a resistor chain behind the keys;
- the control valve that shapes each note's attack;
- the tone-forming network of coils and capacitors that the tablets switch;
- the knee lever, the three-speed vibrato, the Concert model's sub-octaves, and the deliberately distorting amplifier.

It works from the original circuit diagrams. [docs/HISTORY.md](docs/HISTORY.md) tells how they were read and
what had to be estimated.

## Installing

1. Get `clavioline.fwsc` from this repository's **Releases** page, or from the latest run on the **Actions** tab.
2. Install it with the [FM-1 Firmware Hub](https://thefirmwarehub.com) in Chrome or Edge, or over USB:
   ```sh
   python3 tools/fm1_install.py clavioline.fwsc
   ```
3. At power-on the screen shows `BUILD` and a seven-character code: the commit the firmware was built from. Compare it
   with the latest commit on GitHub to be sure you have the newest build.
4. To go back to the official firmware or another one, use the Firmware Hub. It always reaches this firmware over USB.
5. If anything goes wrong, hold **OCT−** while switching on. That starts the USB rescue: no sound, only the updater.

Installing firmware is at your own risk; keep a backup of your FM-1.

## Playing

The Clavioline plays **one note at a time**. When you hold several keys, the **highest** one sounds, as on the original.
Sliding from key to key without letting go changes the pitch without starting a new note. Every new note after
silence starts the same way.

### PLAY mode

| Control | What it does | On the original |
|---|---|---|
| Keys | Play. MIDI notes in (USB or the TRS jack) play too | The 36-key keyboard, F to E |
| **PRESETS** knob | **Knee lever**: the expression swell. Turn quickly for big moves, slowly for small ones. Fully left is silent. MIDI CC 11 moves it too | The lever under the keyboard, pushed with the knee |
| MASTER | Volume | The amplifier |
| OCT− / OCT+ | Octave coupler: LOW / MED / HIGH | The transposing slider |
| ALGORITHM | Shift the FM-1's 27 keys up an octave to reach the top of the 36-key range | |
| SELECT | Presets (see below) | The voicing card |
| GLO | STOPS mode: all the tablets, including vibrato, P and the sub-octaves | The tablet rail |
| SAVE | Store the current tablets and coupler in the user slot shown | |
| KNOB 1 | Fine tuning, ±50 cents | The tuning control |

Plucked sounds work as on the original. Choose Banjo, Mandolin or Harpsichord (they have **P** down), then flick the
knee lever up and back down for each note. Bowed swells are a slow push and release.

### STOPS mode: the tablets

Press **GLO** and the keyboard becomes the row of tablets. Each key turns one on or off, and its LED shows which are
down. **GLO** or **HOME** goes back to playing; nothing is lost.

To hear the tablets while you set them:
- **FX, SEL, ENV, LFO, EDIT** play **C4, D4, E4, F4, G4** for as long as you hold them; their lights glow in STOPS mode.
- **PLAY** holds the last note as a drone.

The bottom line shows the audio engine's CPU load.

```
 black keys:    F#3  G#3  A#3   C#4                         A#4   C#5   D#5   F#5
                vib  vib  vib   vibrato                     Sub   Sub   Sub   Sub
                III  II   I     depth (+)                   I p   II p  I f   II f
 white keys: F3  G3  A3  B3  C4  D4  E4  F4  G4  A4  B4  C5  D5  E5  F5
              1   2   3   4   5   6   7   8   9   O   A   B   V   P   (all off)
```

- **1–9, O, A, B, V** switch the coils and capacitors of the tone-forming network. They combine as in the real circuit:
  each changes what the others do, rather than adding a fixed EQ curve.
- **P** shortens the attack. The control valve then opens fast enough that its current step rings the tone network:
  a short percussive ping at the start of each note, coloured by the tablets that are down.
- **Sub I / Sub II** add the octave and the two octaves below, softly (**p**) or loudly (**f**). They come from the Concert model.

### Presets

**SELECT** steps through 40 voicings, then 4 user slots:
- **17** from the 1952 *Electronic Engineering* table, from Violin to Musical Saw;
- **22** from the Selmer voicing tables quoted by *Sound On Sound*, such as Alto Sax, Church Organ and Harpsichord;
- **an organ tone** from the Concert's sub-octaves.

A `*` after the name means you have changed something since choosing it.

## How it matches the original

| The original | Here |
|---|---|
| Valve multivibrator, retuned by a precision resistor chain; highest key wins | A phase-accurate oscillator with highest-note priority, legato and restart-from-rest |
| Output "trapezoidal in form" | A trapezoid with sloped edges, alias-free |
| The control valve (6BA6) opens after a delay set by its cathode circuit; tablet P shortens it | The printed cathode circuit simulated exactly: ~19 ms normally, ~1.5 ms with P, with the valve's gradual turn-on |
| A network of five coils and 13 capacitor and resistor groups, switched by tablets 1–9, O, A, B, V | **The whole network simulated, component by component**, every sample. The tablets interact because they share one circuit |
| The knee lever is a variable resistor inside that network | The same: softer playing also changes the tone, as the patent describes |
| Vibrato at 4.5 / 5.5 / 6.5 Hz, two depths | The same rates; depths in the ratio the circuit gives (3.8 : 1) |
| Concert: Sub I / Sub II from divider valves, soft or loud | Square-wave dividers clocked by the oscillator |
| An amplifier built to distort, mostly 2nd harmonic | A valve-style stage adding mainly 2nd harmonic |

Some things are not in any surviving document: the coils' values, the knee lever's exact taper, the vibrato's depth in
cents and the amount of distortion. They are estimated, and every estimate is listed in
[docs/ENGINE.md](docs/ENGINE.md), so you can change them.

## Documentation

- [docs/HISTORY.md](docs/HISTORY.md): the instrument, the sources, how the circuit was reconstructed, and the open questions.
- [docs/ENGINE.md](docs/ENGINE.md): how the sound engine works, every parameter and estimate, the tests, and building from source.
- [docs/platform/](docs/platform/): the FM-1 platform's API and the chip's rules, for developers.

## Credits and licence

GPL-3.0-only. The FM-1 platform is [Dinghy](https://github.com/zvenson/dinghy) by zvenson, built on Felucca by Leo Kuroshita (Hügelton Instruments)
and its successors. See [LICENSING.md](LICENSING.md) for who made what.

"Clavioline" was a name used by Constant Martin and Selmer. "M-VAVE" and "FM-1" belong to their owners. This firmware is not made or endorsed by any of them.
