# SG-9 Web Engine Port Spec → Native C++ DSP

This document specifies the SG-9 web app's audio engine (extracted 2026-10-08
from the shipping `gui-mockup` web artifact) for a faithful C++ port into
`dsp/Sg9Dsp.{h,cpp}`. The existing native DSP is the OLD 70s-saw engine and
must be REPLACED by this engine. Keep the public class interface
(prepare/reset/setParams/process/triggerVoice/releaseVoice/getActiveTrigger/
getLatencySamples) so the JUCE wrapper keeps compiling.

Sample rate: parameterize on prepare(); design at 48k but must work at 44.1k.

## 1. Chakras (9 voices, monophonic)

| idx | name | Hz |
|-----|------|-----|
| 0 | Crown | 963 |
| 1 | Third Eye | 852 |
| 2 | Throat | 741 |
| 3 | Heart | 639 |
| 4 | Solar Plexus | 528 |
| 5 | Sacral | 417 (DEFAULT active) |
| 6 | Root | 396 |
| 7 | Renewal | 285 |
| 8 | Foundation | 174 |

Monophonic: triggerVoice(i) starts voice i with ~3.2 s exponential attack to
peak; triggering another voice releases the old over ~1.5 s; tapping the
active voice stops all sound. Peak = 0.22 * (level/100)^1.45, level = per-voice
0..100 (defaults: 55,48,52,60,68,58,62,42,46).

## 2. Drone (per active chakra)

Reed wavetables (17 Fourier coeffs, sine phase, index = harmonic #):
- reedWave (neutral):        [0,1,.38,.30,.18,0,.12,.10,.075,.06,0,.038,.032,.025,.021,.018,.014]
- majorReedWave:             [0,1,.38,.30,.18,.14,.12,.10,.075,.06,.045,.038,.032,.025,.021,.018,.014]
- jawariWave:                [0,.30,.50,.38,.27,0,.18,.15,.12,.10,0,.07,.06,.05,.04,.035,.03]

Implement as band-limited wavetable oscillators (sum sines; fine at these
frequencies) OR precomputed periodic waves — either is acceptable.

Shruti interval sets (drone tone selector, DEFAULT = paSaSa):
- saPaSa "Sa–Pa–Sa": ratios [1, 3/2, 2],     levels [.27,.16,.13]
- sa     "Sa":       ratios [1, 1, 1],       levels [.38,0,0]
- saSa   "Sa–Sa":    ratios [1, 2, 2],       levels [.30,.16,0]
- saMaSa "Sa–Ma–Sa": ratios [1, 4/3, 2],     levels [.27,.16,.13]
- paSaSa "Pa–Sa–Sa": ratios [3/4, 1, 2],     levels [.18,.26,.12]

Each open reed = a close-miked PAIR: two wavetable oscs at the same freq,
detuned by cents, gains .62/.38:
- core pair:  freq = base*ratios[0], detune (-1.7,+1.9), gain = levels[0], pan 0
- left pair:  freq = base*ratios[1], detune (-2.3,+1.4), gain = levels[1], pan -.34
- right pair: freq = base*ratios[2], detune (-1.2,+2.5), gain = levels[2], pan +.34

Octave doublings (smaller detunes):
- sL: 2*base*ratios[0], detune (-.9,+1.1), gain levels[0]*.055, pan -.22
- sR: 2*base*ratios[1], detune (-1.1,+.8), gain levels[1]*.055, pan +.22
- h2: 2*base*ratios[2], detune (-.8,+1.0), gain levels[2]*.045, pan +.24
- h3: 3*base,            detune (-.7,+.9),  gain .0075,            pan -.10

Jawari shimmer path: highpass 520 Hz (Q .18) → lowpass 3100 Hz (Q .16) →
gain .072. Three jawariWave oscs at base*ratios[0..2], detunes +.6/-1.1/+1.4,
gains = mode.levels[0..2].

Harmony color reeds (ALWAYS present; gains follow pad voicing — see §7):
- minorLo 6/5*base,  reedWave, pans ∓.14, gains .038/.006 if minorThird else 0
- minorHi 12/5*base, reedWave
- majorLo 5/4*base,  majorReedWave, pans ∓.18, gains .042/.008 if majorThird else 0
- majorHi 5/2*base,  majorReedWave

Drone body filter: lowpass, Q .18. baseCut = clamp(base*3.2, 1900, 3600).
Cutoff starts at baseCut*.76, eases to baseCut (tau ~3.4 s).

All-pass drift (NO formants, NO ring mod): dry .90 + allpass chain
(300 Hz/Q.30 → 610/.27 → 1080/.23), send .10, return .66. Three sine LFOs
modulate the allpass freqs: 1/97 Hz depth 45 Hz, 1/131 depth 68, 1/173 depth 92.
Stereo orbit panner LFO 1/43 Hz, depth .09.

Slow drift tracks (start at trigger+38 s, re-ramp every 60–180 s to a random
target within ±depth, clamped):
- filter freq: ±7%
- core gain: ±5.5%
- o1b detune: ±18% around 1.9 cents (clamp 1.45–2.35)
- jawari bus: ±16% around .072
- h3 gain: ±18% around .0075

Pre-fader: drone bus gets FIXED 1.4x gain before the DRONE fader.

## 3. Pads (Deep Blue steel phrase)

7 voicings (pad voicing selector, DEFAULT = majorThird):
- octaves:    [1/2,1,2,4]
- tanpura:    [1/2,1,3/2,2,3]
- fourths:    [1/2,1,4/3,2,8/3]
- septimal:   [3/4,1,3/2,7/4,2]
- shruti:     [1/2,1,9/8,10/9,2]
- minorThird: [1/2,1,6/5,3/2,2]
- majorThird: [1/2,1,5/4,2,5/2]

Phrase: step 6 s, note length 8 s, swell 4 s, release 2 s, glide .92 s.
Swell curve = double smoothstep over 65 points: s(x)=x²(3−2x); c=s(s(x)).
Release curve (12 pts): [1,.98,.94,.86,.72,.53,.34,.18,.075,.022,.004,.0001].
Overlap: release begins exactly when the next step starts (crossfade).

Note voice: sine osc + triangle companion (gain .10, freq ×1.0014).
Glide: exponential ramp from previous ratio's freq to target over .92 s.
Arrival error: pattern [0,3,-2,4,-3,5,-2,3,-4,2,-3,1] cents by step index.
Lowpass: starts min(2700,max(1350,target*4.2)), → min(1120,max(500,target*1.55))
over 2.8 s, then settles min(920,max(420,target*1.24)).
Vibrato: 4.9 Hz + pattern [0,.12,-.08,.18,-.14,.1,-.06,.16,-.1,.08,-.12,.14,-.05,.11,-.09,.04],
depth 12 cents + breath-modulated 9 cents (breath = same 10.9 s cycle as §6).
Pan per step: [-.42,-.18,.08,.32,.55,.72,.48,.18,-.12,-.36,-.58,-.16].
Pad base gain −4 dB; direct path .22, reverse path .92.

Reverse pre-echo: triangle at target*.5, starts 2.35 s before note, lowpass
420→1240 Hz, env to .17, pan = −directPan*.82. Reads as reversed tape.

Pad delays (fed from pads bus):
- Steel: 8 feed-forward taps at .5,1,1.5,…,4 s; levels
  [.32,.285,.25,.22,.195,.17,.15,.13]; pans [-.96,.96,-.88,.88,-.98,.98,-.92,.92];
  per-tap lowpass 1240−idx*80 Hz; feedback .65 through 720 Hz lowpass; wet .62.
- Reverse: 4 stages of .72 s; levels [.10,.20,.36,.57]; pans alternate ∓.94;
  feedback .38 through 860 Hz lowpass; wet .42 → space delay, .72 → spaceIn.

## 4. Pulse (sparse pitch-dive toms)

- New chakra: 3 FULL pattern cycles with NO toms (grace). Then on each pattern
  downbeat (beat 0), if (cycle−3) is even → one tom. Cycles counted per pattern.
- Steps alternate phrases: SUB (ratio .5, gain .13), ROOT (1, .115).
- Tom: two sines (body + sub-octave weight at .055 gain). Start freq =
  min(1320, max(target*1.32, target+34)); exponential dive to target over 72%
  of decay. Decay = 2.55 + (phrase%3)*0.35 s (2.55–3.25). Lowpass from
  min(1450,max(310,start*1.08)) → min(980,max(180,target*1.3)). Env: attack .065 s
  to step.gain, → 48% at half decay, → .0001 at end. Pan ∓.18 (SUB left).
  Plus a muted noise fleck: lowpass min(720,max(210,target*.9)), peak .0045.
- Dub siren every 4th phrase: f/4, 8.4 s, start 1.06× → target, gain .006,
  lowpass min(280,max(90,target*1.18)), dark echo (delay .72 s, fb .31, wet .13).
- Pulse bus gain = 0.52 + 0.22*(voiceLevel/100).

## 5. Beat (electronic DUM/TAK/riq machine — NEVER a rock backbeat)

Tempo sources (BPM): Sun 118.3, Earth OM 127.6, Earth Day 121.4, Moon 98.6,
Schumann 117.4. Speed scale: Full 1, Half .5, Quarter .25 (DEFAULT).
DEFAULT source = Earth Day, DEFAULT speed = Quarter (→ 30.35 BPM).

Patterns (DEFAULT = baladi). Each: beats, bols[], bass[], bayan[], dayan[], hand[]:
- baladi:  8, [DUM,DUM,riq,TAK,DUM,riq,TAK,riq], bass[0,1,4], bayan[0,4], dayan[3,6], hand[2,5,7]
- maqsum:  8, [DUM,TAK,riq,TAK,DUM,riq,TAK,riq], bass[0,4], bayan[0], dayan[1,3,6], hand[2,5,7]
- malfuf:  2, [DUM,TAK], bass[0], bayan[0], dayan[1], hand[1]
- keherwa: 8, [Dha,Ge,Na,Ti,Na,Ka,Dhi,Na], bass[0,4], bayan[0,1,6], dayan[2,4,7], hand[3,5]
- dadra:   6, [Dha,Dhin,Na,Dha,Tin,Na], bass[0,3], bayan[0,3], dayan[1,2,4,5], hand[2,5]
- rupak:   7, [Tin,Tin,Na,Dhin,Na,Dhin,Na], bass[3,5], bayan[3,5], dayan[0,2,4,6], hand[1,4,6]
- jhaptal: 10,[Dhi,Na,Dhi,Dhi,Na,Tin,Na,Dhi,Dhi,Na], bass[0,2,7], bayan[0,3,7], dayan[1,4,5,8], hand[1,4,6,9]
- ektaal:  12,[Dhin,Dhin,DhaGe,TiRaKiTa,Tun,Na,Kat,Ta,DhaGe,TiRaKiTa,Dhin,Na], bass[0,2,8,10], bayan[0,8], dayan[1,4,6,10], hand[3,5,7,9,11]
- teental: 16,[Dha,Dhin,Dhin,Dha,Dha,Dhin,Dhin,Dha,Dha,Tin,Tin,Ta,Ta,Dhin,Dhin,Dha], bass[0,4,12], bayan[0,4,12], dayan[2,6,9,10,13,15], hand[3,7,8,11,15]

Voices (all tuned to the ACTIVE chakra via foldFrequency: fold hz into [lo,hi]
by octaves):
- DUM (bass positions): sine, root=fold(f/8,48,74), 1.12×→root over .18 s,
  second harmonic ×2 at .065, lowpass min(360,max(145,root*3.1)), decay
  clamp(beatSec*.72, .82, 1.75) s, level .27 (downbeat) / .22.
- Analog kick (same positions): sine 76→48 (exp, .16 s) →38 Hz; lowpass 170;
  decay clamp(beatSec*.56,.62,1.35); level .115/.09.
- Bayan (soft tom): sine root=fold(f/8,46,82), 1.28×→root over .30 s,
  sub at ×.5 gain .045, lowpass 235, decay .78 s, level .12/.095, pan −.18.
- TAK/woodblock (dayan positions, EVERY OTHER written occurrence):
  sine hz=fold(f*1.8,620,1050), 1.025×→hz over .075 s, bandpass at hz Q .72,
  env .022 attack → .25 s decay, level .09/.078, pan +.24; plus noise fleck
  bandpass 1120 Q .46, peak .09×level. → feeds 6 ping-pong taps:
  eighth-note spacing (recomputed on tempo change), levels
  [.125,.086,.059,.040,.027,.018], pans [-.72,.72,-.68,.68,-.64,.64],
  per-tap lowpass 920−idx*90 Hz. NO FEEDBACK — feed-forward taps only.
- Hand (riq+shaker): frame drum sine hz=fold(f*.42,110,190), 1.12×→hz,
  lowpass 460, decay .32, level .072; jingle noise HP1450→LP3800, peak .2×level;
  shaker noise HP240→LP fold(f,480,820), decay .34, level .016. Pans alternate.

Drum bus: 4-beat pan orbit, depth .38, quarter-sine curves per beat;
lowpass 2600 → tanh(1.25x)/tanh(1.25) waveshaper (2x) → compressor
(thr −5 dB, knee 5, ratio 2.2, att .008, rel .075).

## 6. Space

- 30 s stereo convolver, IR = noise * (1−t)^0.68. Mid/side: mid gains .42/.42,
  side 1.20/−1.20 (240% width), decorrelation delays .013/.067 s.
- Returns scale with SPACE fader (default .68): reverb ×.58, delay ×.50.
- Return EQ: highpass 250 (Q .62) → peaking −9 dB @720 (Q .48) →
  peaking −5.5 dB @1550 (Q .78) → highshelf +1.5 dB @4800.
- Drone delay: .56 s, lowpass 760, in .34, out .24 → space delay return.
- delayBloom .46: space delay return → spaceIn (reverb input).
- Sends (post-fader): drone, pulse, pads, drums → spaceIn.

## 7. Master routing & defaults

- dronePre(1.4) → drone fader → {breathBed, spaceIn, droneDelayIn}
- pads → pads fader → {breathBed, spaceIn, steelDelayIn, reverseDelayIn}
- breathBed: base .78, depth .22, 10.9 s cycle (4.4 s inhale smoothstep up,
  6.5 s exhale smoothstep down, no plateau) → master
- pulse → pulse fader → analyser → waveshaper → {master, spaceIn}
- drums → drums fader → drumTone → waveshaper → comp → {master, spaceIn}
- felt bass: 40 Hz sine → lowpass 88 → gain .024 → master (always on)
- NO compressor on drone/pads path. NO noise/hiss source anywhere.
- Fader defaults: DRONE 1.00, PULSE .69, PADS .32, BEAT .92, SPACE .68.
- Param defaults: droneTone=paSaSa, tempoSource=Earth Day, padVoicing=majorThird,
  beatSpeed=Quarter(.25), beatPattern=baladi.

## 8. Harmony-follow rule (drone ↔ pads)

Pad voicing minorThird → minor color reeds at gains .038/.006, major at 0.
Pad voicing majorThird → major color reeds at .042/.008, minor at 0.
All other voicings → both at 0. Changes apply live with ~.28 s smoothing.

## 9. What NOT to port

- pulseVisual/beatVisual, DOM, CSS, glow (UI only)
- scheduleHeartbeat, scheduleShimmerTick (dead code, never called)
- mantra bells, COSMOS bed, resPan, session macro, ascension (old engine)
- MIDI nearest-chakra: keep noteOn/noteOff mapping to nearest chakra trigger
  (existing behavior), TRUE/FOLLOW transpose semantics unchanged.
