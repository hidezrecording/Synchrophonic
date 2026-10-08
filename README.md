# SG-9 — Solfeggio Drone Instrument (working title)

Nathan's second sellable audio plugin: a drone instrument built on the nine
solfeggio frequencies (174, 285, 396, 417, 528, 639, 741, 852, 963 Hz), voiced as a
chakra column with an Evolution macro and a guided root-to-crown Ascension
sweep for evolving ambient textures.

> **Name status:** "SG-9" is a working title only — Nathan has not formally
> approved it. The code uses `Sg9` / `sg9` throughout so a rename is mechanical.

## Layout

```
sg9/
├── dsp/            # framework-free C++17 DSP core (no JUCE dependency)
│   ├── Sg9Dsp.h
│   └── Sg9Dsp.cpp
├── juce/           # JUCE 8 wrapper: parameters, MIDI, DAW I/O, GUI
│   ├── Sg9Processor.{h,cpp}
│   ├── Sg9Editor.{h,cpp}      # photo-as-interface GUI (black bg, twin glass VU meters)
│   ├── CMakeLists.txt
│   └── assets/chakra-figure.jpg  # embedded illustration (BinaryData)
├── tests/
│   ├── test_sg9.cpp                      # DSP invariant tests (runs here)
│   └── test_sg9_processor_headless.cpp   # real processor path test (runs here)
└── .github/workflows/build.yml           # CI: macOS VST3+AU, Windows VST3
```

## The sound

Nine sine-oscillator voices at the solfeggio frequencies, each with a stereo
detuned double (±4¢), ultra-slow AM LFOs (0.02–0.15 Hz), smoothed random-walk
pitch drift (±5¢), an optional BODY harmonic shimmer (2f+3f partials), and a
level. Master chain: modulated 2-pole lowpass (Tone) → algorithmic shimmer
reverb (octave-up feedback, Shimmer/Space) → dotted multi-tap stereo delay
(Echo) → pink-noise Air bed → soft clipper with a true ±0.99 ceiling.

- **Evolution** (0–1): one macro scaling every LFO depth and the ascension
  morph rate. Mod wheel (CC1) overrides it.
- **Ascension**: guided energy sweep root→crown (or crown→root) over 1–30
  minutes; each voice swells as the energy passes its spine position.
- **TRUE / FOLLOW**: absolute solfeggio frequencies, or MIDI note transposes
  the whole set (note − 60 semitones). **Drone latch** ignores note-offs;
  **Gate** silences on note-off.

## Planetary resonance layer

Three additions for long listening sessions. None of them touch the solfeggio
fundamentals — pitch purity is preserved by design.

- **COSMOS bed**: four continuous voices at Hans Cousto's "Cosmic Octave"
  frequencies — Sun 126.22, Earth OM 136.10, Earth Day 194.18, Moon 210.42 Hz.
  These are *octave transpositions of orbital periods* (Cousto, 1978), a tuning
  philosophy — **not measured physics**. Same 70s pad architecture as the
  chakras (detuned saws ±3¢, lowpass, wow/flutter), sitting **−6 dB** under
  them. The bed is never part of monophonic chakra triggering — it can't be
  stolen, and triggering chakras never interrupts it. Group toggle + level.
- **Binaural resonance pan** (master stereo, headphones recommended):
  antiphase slow pan, L = 1+d·sin(2πft), R = 1−d·sin(2πft), normalized by
  1/(1+d) so the absolute ceiling holds and L+R stays constant (no mono
  pumping). Rate selectable: **7.83 / 14.3 / 20.8 Hz** — the first three
  **measured** Schumann Earth-ionosphere cavity resonances (not exact
  harmonics; labeled as such). Depth 0–0.5, default 0.15.
- **Long-session pleasing**: a **SESSION macro** (default on) runs one
  ultra-slow 10-minute sinusoidal cycle morphing filter base ±15%, chorus
  depth ±20%, delay mix ±15% — always smooth, never stepping. After a voice's
  20 s evolve completes, its filter eases −10% into a "resting" state.
  All beating stays slow and consonant, attacks/releases soft.

## Rhythmic layer — PULSE + DRUMS

Two additions tied directly to the frequencies in use. None of them touch
the solfeggio fundamentals — pitch purity is preserved by design.

- **Pulse** (default on): the rhythmic pulse — a tight 8-step cycle
  (~1.2 s per step, ~9.6 s per cycle) of soft muted tones tuned to the
  ACTIVE chakra voice — LOW at f/2, MID at f, HIGH at 2f (pure octaves),
  plus a deep heartbeat at f/4, shimmer at 4f, Eno bass pulse at f/2 and
  twang at 2f. All muted, all locked to the chakra. Monophonic: restarts
  on each chakra touch, retunes to the new fundamental. Level 0–1
  (default 0.6), pace 0.5–1.5 (default 1.0).
- **Drums** (default on): an electronic tabla drum track for Hindu trance.
  The tempo follows the planetary resonances (octave-reduced): Sun 118 BPM,
  Earth OM 128, Earth Day 121, Moon 99 — or Cycle (slow morph). Four
  patterns: Keherwa (8), Dadra (6), Rupak (7), Teental (16). The tabla
  dayan (treble) is tuned to the chakra fundamental (f), the bayan (bass)
  to f/2, the bass drum to f/4, with hand-percussion shaker. All locked to
  the chakra. Level 0–1 (default 0.6).
- **Breath** (default on): a global master swell at **5.5 breaths/min**
  (0.0917 Hz, the resonant pranayama rate), ±(0–12%) amplitude by depth
  (default 0.5 → ±6%). Applied post-everything — after the resonance pan —
  so the breathing is a pure master-level motion that can never skew the
  stereo image. Phase randomized per render from a deterministic seed.

## Eno evolution (generative drone drift)

The drone evolves like Eno's ambient works — very slow (60-180 s periods),
non-repetitive generative drift: the filter cutoff wanders 400-1100 Hz and
the 2f/3f/4f harmonic levels breathe independently. Never static, never
looping.

## DEEP BLUE pads (wide evolving textures)

In the spirit of Brian Eno's "Deep Blue Day": three detuned stereo pairs
at f/2, f, 2f of the active chakra, spread hard left/right (±8 cents, slow
beating). Every voice blooms over ~3 s (backward-attack, never strikes);
slow drift LFOs at incommensurable rates (0.05/0.073/0.11 Hz) wander the
amplitude so the texture never exactly repeats — infinite evolution. On a
chakra change the pads portamento (~4 s) to the new tuning instead of
cutting. Quiet under the drone (-12 dB). Level 0–1 (default 0.5).

## Mixer

Six faders bring each layer in and out: DRONE (the nine chakra voices),
PULSE (the rhythmic pulse), PADS (deep blue), DRUMS (tabla track),
SPACE (the shimmer reverb wash).

## GUI direction (per Nathan)

Photo-as-interface: his chakra-figure illustration full-bleed on a **black**
background — no Bitey/RCA hardware aesthetic. Nine circular hit zones on the
chakra symbols (vertical drag = level, glow = live voice level, tap = select).
Two stereo VU meters, one on each side of the head, in the same functional
glass style as Bitey's (recessed under glass, live needles driven by L/R
output). Overlay chips only: Sound, Ascension run/stop, Asc/Desc, selected
voice readout.

## Build & test (Linux dev box)

```bash
# DSP unit tests (12 tests) — compiles and RUNS here:
g++ -std=c++17 -O2 -Wall tests/test_sg9.cpp dsp/Sg9Dsp.cpp -o build/test_sg9
./build/test_sg9

# Headless processor test — compiles the REAL Sg9Processor.cpp against the
# real JUCE 8.0.6 modules (prebuilt objects in ../bitey/build/juce_headless)
# and drives processBlock() like a DAW. GUI excluded via SG9_HEADLESS:
g++ -std=c++17 -O2 -DSG9_HEADLESS -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_USE_CURL=0 -I~/workspace/vendor/juce-8.0.6/modules -Ijuce -I. \
    tests/test_sg9_processor_headless.cpp juce/Sg9Processor.cpp dsp/Sg9Dsp.cpp \
    ../bitey/build/juce_headless/*.o -o build/test_sg9_processor -lpthread -ldl
./build/test_sg9_processor

# Editor syntax check (no full JUCE link possible on this box):
g++ -std=c++17 -fsyntax-only -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_USE_CURL=0 -I~/workspace/vendor/juce-8.0.6/modules -Ijuce -I. \
    -I/tmp/sg9_stubs juce/Sg9Editor.cpp   # /tmp/sg9_stubs/BinaryData.h stub
```

The first TRUE compile of the plugin (VST3/AU) happens on CI. The `juce/`
wrapper was syntax-checked against the real JUCE 8.0.6 headers; JUCE 8 API
breaks seen on Bitey (nested `AudioProcessor::BusesProperties`, static
`copyXmlToBinary`/`getXmlFromBinary`) are accounted for.

## Licensing note

JUCE 8 is **AGPLv3-or-commercial** (verify current tiers at juce.com). A
closed-source paid plugin needs a commercial JUCE license. The DSP core in
`dsp/` has no JUCE dependency, so the wrapper could be re-targeted at iPlug2
(MIT) later without touching the sound.

## Status / blockers

See `~/workspace/goals/bitey-sg-9-solfeggio-drone-plugin/hidden_files/build-log.md`.
To ship: Nathan must create the GitHub repo and supply a one-time PAT (same
flow as Bitey), then push this tree; CI builds the first binaries.
