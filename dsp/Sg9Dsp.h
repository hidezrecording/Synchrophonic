// SG-9 — framework-free C++17 DSP core (web-engine port).
//
// Faithful native port of the shipping gui-mockup web artifact's audio
// engine (spec: ../WEB_ENGINE_SPEC.md, extracted 2026-10-08). This REPLACES
// the old 70s-saw engine entirely.
//
// Architecture (all sample-rate parameterized, design at 48 kHz):
//   9 monophonic chakra voices -> shruti reed drone (wavetable reed pairs +
//   jawari shimmer + harmony-follow color reeds + all-pass drift)
//   Deep Blue steel pads (7 just-intonation voicings, 6 s phrase, reverse
//   pre-echo, steel + reverse feed-forward delays)
//   Pulse (sparse pitch-dive toms, 3-cycle grace) + Beat (electronic
//   DUM/TAK/riq machine, 9 patterns, 5 tempo sources, woodblock ping-pong taps)
//   Space (30 s partitioned convolver, mid/side width, airy return EQ)
//   Master (breath bed, felt bass, tanh stages, drum-bus compressor)
//
// Real-time safety: every buffer is preallocated in prepare(); the hot path
// performs zero allocations. All randomness comes from SeededRng instances —
// two instances with identical params and call sequences render identically.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sg9 {

constexpr double kPi = 3.14159265358979323846;
constexpr float kDenormalNudge = 1e-18f; // keeps feedback loops denormal-safe

// ---------------------------------------------------------------------------
// 1. Chakras (9 voices, monophonic)
// ---------------------------------------------------------------------------
constexpr int kNumChakras = 9;

struct ChakraSpec {
    const char* name;
    float freq;
};

constexpr ChakraSpec kChakras[kNumChakras] = {
    { "Crown",        963.0f },
    { "Third Eye",    852.0f },
    { "Throat",       741.0f },
    { "Heart",        639.0f },
    { "Solar Plexus", 528.0f },
    { "Sacral",       417.0f }, // DEFAULT active voice
    { "Root",         396.0f },
    { "Renewal",      285.0f },
    { "Foundation",   174.0f },
};

constexpr int kDefaultVoice = 5; // Sacral

// Voice envelope: ~3.2 s exponential attack to peak, ~1.5 s release.
constexpr float kVoiceAttackSec  = 3.2f;
constexpr float kVoiceReleaseSec = 1.5f;
// Peak = 0.22 * (level/100)^1.45
constexpr float kVoicePeakGain = 0.22f;
constexpr float kVoicePeakExp  = 1.45f;

// ---------------------------------------------------------------------------
// 2. Shruti drone interval sets (drone tone selector)
// ---------------------------------------------------------------------------
enum DroneTone : int {
    kDroneSaPaSa = 0,
    kDroneSa     = 1,
    kDroneSaSa   = 2,
    kDroneSaMaSa = 3,
    kDronePaSaSa = 4,
    kNumDroneTones = 5,
};

struct ShrutiMode {
    const char* name;
    float ratios[3];
    float levels[3];
};

constexpr ShrutiMode kShrutiModes[kNumDroneTones] = {
    { "Sa-Pa-Sa", { 1.0f, 1.5f, 2.0f },      { 0.27f, 0.16f, 0.13f } },
    { "Sa",       { 1.0f, 1.0f, 1.0f },      { 0.38f, 0.0f,  0.0f  } },
    { "Sa-Sa",    { 1.0f, 2.0f, 2.0f },      { 0.30f, 0.16f, 0.0f  } },
    { "Sa-Ma-Sa", { 1.0f, 4.0f/3.0f, 2.0f }, { 0.27f, 0.16f, 0.13f } },
    { "Pa-Sa-Sa", { 0.75f, 1.0f, 2.0f },     { 0.18f, 0.26f, 0.12f } },
};

constexpr int kDefaultDroneTone = kDronePaSaSa;

// Reed pair voicing: two wavetable oscs per open reed, gains .62/.38.
// Detunes widened 2026-10-08 per Nathan ("more beating, less organ").
constexpr float kReedPairDetuneCore[2]  = { -2.6f, 2.9f }; // cents
constexpr float kReedPairDetuneLeft[2]  = { -3.4f, 2.2f };
constexpr float kReedPairDetuneRight[2] = { -1.9f, 3.6f };
constexpr float kReedPairGainA = 0.62f;
constexpr float kReedPairGainB = 0.38f;
// Pans widened 2026-10-08 per Nathan (stereo width like a real instrument).
constexpr float kReedPanCore  = 0.0f;
constexpr float kReedPanLeft  = -0.55f;
constexpr float kReedPanRight =  0.55f;

// Octave doublings (smaller detunes).
constexpr float kOctDetuneSL[2] = { -0.9f, 1.1f };
constexpr float kOctDetuneSR[2] = { -1.1f, 0.8f };
constexpr float kOctDetuneH2[2] = { -0.8f, 1.0f };
constexpr float kOctDetuneH3[2] = { -0.7f, 0.9f };
constexpr float kOctPanSL = -0.34f;
constexpr float kOctPanSR =  0.34f;
constexpr float kOctPanH2 =  0.36f;
constexpr float kOctPanH3 = -0.18f;
constexpr float kOctGainScaleA = 0.055f; // sL / sR relative to mode level
constexpr float kOctGainScaleB = 0.045f; // h2 relative to mode level
constexpr float kH3BaseGain    = 0.0075f;

// Jawari shimmer path.
constexpr float kJawariHpFreq = 520.0f;
constexpr float kJawariHpQ    = 0.18f;
constexpr float kJawariLpFreq = 3100.0f;
constexpr float kJawariLpQ     = 0.16f;
constexpr float kJawariBaseGain = 0.09f; // up from .072 per Nathan 2026-10-08
constexpr float kJawariDetune[3] = { 0.6f, -1.1f, 1.4f };

// Harmony-follow color reeds (gains set live from pad voicing, see §8).
constexpr float kMinorLoRatio = 6.0f/5.0f;
constexpr float kMinorHiRatio = 12.0f/5.0f;
constexpr float kMajorLoRatio = 5.0f/4.0f;
constexpr float kMajorHiRatio = 5.0f/2.0f;
constexpr float kMinorPan = 0.14f;
constexpr float kMajorPan = 0.18f;
constexpr float kMinorGainA = 0.038f;
constexpr float kMinorGainB = 0.006f;
constexpr float kMajorGainA = 0.042f;
constexpr float kMajorGainB = 0.008f;
constexpr float kHarmonySmoothSec = 0.28f; // live gain smoothing

// Drone body filter: lowpass Q .18, baseCut = clamp(base*3.2, 1900, 3600),
// cutoff eases from baseCut*.76 to baseCut with tau ~3.4 s.
constexpr float kBodyFilterQ = 0.18f;
constexpr float kBodyCutMin  = 1900.0f;
constexpr float kBodyCutMax  = 3600.0f;
constexpr float kBodyCutStartScale = 0.76f;
constexpr float kBodyCutTauSec     = 3.4f;

// All-pass drift (NO formants, NO ring mod): dry .90 + all-pass chain,
// send .10, return .66. LFOs modulate all-pass freqs.
constexpr float kApDry    = 0.86f;
constexpr float kApSend   = 0.16f; // raised 2026-10-08 for audible stereo
constexpr float kApReturn = 0.68f; // decorrelation (Nathan: real-instrument width)
constexpr float kApFreq[3]      = { 300.0f, 610.0f, 1080.0f };
constexpr float kApQ[3]         = { 0.30f, 0.27f, 0.23f };
constexpr float kApLfoHz[3]     = { 1.0f/97.0f, 1.0f/131.0f, 1.0f/173.0f };
constexpr float kApLfoDepthHz[3]= { 45.0f, 68.0f, 92.0f };
constexpr float kOrbitLfoHz     = 1.0f/43.0f;
constexpr float kOrbitLfoDepth  = 0.16f; // deepened 2026-10-08 per Nathan
// Bellows pump: hand-driven amplitude movement on the drone (anti-organ).
// Slow, deep, slightly irregular feel via two detuned LFOs.
constexpr float kBellowsLfoHz    = 0.13f;
constexpr float kBellowsLfo2Hz   = 0.191f;
constexpr float kBellowsDepth    = 0.14f;

// Slow drift tracks: start at trigger+38 s, re-ramp every 60-180 s to a
// random target within +/-depth, clamped.
constexpr float kDriftStartSec     = 38.0f;
constexpr float kDriftRetrigMinSec = 60.0f;
constexpr float kDriftRetrigMaxSec = 180.0f;
constexpr float kDriftFilterDepth  = 0.07f;  // +/-7% filter freq
constexpr float kDriftCoreDepth    = 0.055f; // +/-5.5% core gain
constexpr float kDriftDetuneDepth  = 0.18f;  // +/-18% around 1.9 cents
constexpr float kDriftDetuneMinCents = 1.45f;
constexpr float kDriftDetuneMaxCents = 2.35f;
constexpr float kDriftJawariDepth  = 0.16f;  // +/-16% around .072
constexpr float kDriftH3Depth      = 0.18f;  // +/-18% around .0075

// Pre-fader: drone bus gets a FIXED kDronePreGain gain before the DRONE fader.
constexpr float kDronePreGain = 3.2f; // Nathan 2026-10-08: drone must lead;
// was 1.4 (web value); +7.2 dB puts drone ~4.5 dB above pads at defaults.

// ---------------------------------------------------------------------------
// 3. Reed / jawari Fourier coefficient tables (17 coeffs, sine phase,
//    index = harmonic number)
// ---------------------------------------------------------------------------
constexpr int kNumHarmonics = 17;

constexpr float kReedWave[kNumHarmonics] = {
    0.0f, 1.0f, 0.38f, 0.30f, 0.18f, 0.0f, 0.12f, 0.10f,
    0.075f, 0.06f, 0.0f, 0.038f, 0.032f, 0.025f, 0.021f, 0.018f, 0.014f
};
constexpr float kMajorReedWave[kNumHarmonics] = {
    0.0f, 1.0f, 0.38f, 0.30f, 0.18f, 0.14f, 0.12f, 0.10f,
    0.075f, 0.06f, 0.045f, 0.038f, 0.032f, 0.025f, 0.021f, 0.018f, 0.014f
};
constexpr float kJawariWave[kNumHarmonics] = {
    0.0f, 0.30f, 0.50f, 0.38f, 0.27f, 0.0f, 0.18f, 0.15f,
    0.12f, 0.10f, 0.0f, 0.07f, 0.06f, 0.05f, 0.04f, 0.035f, 0.03f
};

// ---------------------------------------------------------------------------
// 4. Pad voicings (Deep Blue steel phrase)
// ---------------------------------------------------------------------------
enum PadVoicing : int {
    kPadOctaves    = 0,
    kPadTanpura    = 1,
    kPadFourths    = 2,
    kPadSeptimal   = 3,
    kPadShruti     = 4,
    kPadMinorThird = 5,
    kPadMajorThird = 6,
    kNumPadVoicings = 7,
};

struct PadVoicingSpec {
    const char* name;
    int count;
    float ratios[5];
};

constexpr PadVoicingSpec kPadVoicings[kNumPadVoicings] = {
    { "Octaves",        4, { 0.5f, 1.0f, 2.0f, 4.0f, 0.0f } },
    { "Tanpura stack",  5, { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f } },
    { "Fourth pillars", 5, { 0.5f, 1.0f, 4.0f/3.0f, 2.0f, 8.0f/3.0f } },
    { "Septimal depth", 5, { 0.75f, 1.0f, 1.5f, 1.75f, 2.0f } },
    { "Shruti shimmer", 5, { 0.5f, 1.0f, 9.0f/8.0f, 10.0f/9.0f, 2.0f } },
    { "Minor third veil", 5, { 0.5f, 1.0f, 6.0f/5.0f, 1.5f, 2.0f } },
    { "Major thirds",   5, { 0.5f, 1.0f, 5.0f/4.0f, 2.0f, 5.0f/2.0f } },
};

constexpr int kDefaultPadVoicing = kPadMajorThird;

// Pad phrase timing (seconds).
constexpr float kPadStepSec    = 6.0f;
constexpr float kPadNoteLenSec = 8.0f;
constexpr float kPadSwellSec   = 4.0f;
constexpr float kPadReleaseSec = 2.0f;
constexpr float kPadGlideSec   = 0.92f;
constexpr int   kPadSwellPts   = 65; // double-smoothstep curve points
constexpr int   kPadReleasePts = 12;
constexpr float kPadReleaseCurve[kPadReleasePts] = {
    1.0f, 0.98f, 0.94f, 0.86f, 0.72f, 0.53f,
    0.34f, 0.18f, 0.075f, 0.022f, 0.004f, 0.0001f
};

// Per-step musical patterns (indexed by step % N).
constexpr int kPadArrivalCents[12] = { 0, 3, -2, 4, -3, 5, -2, 3, -4, 2, -3, 1 };
constexpr float kPadVibPattern[16] = {
    0.0f, 0.12f, -0.08f, 0.18f, -0.14f, 0.10f, -0.06f, 0.16f,
    -0.10f, 0.08f, -0.12f, 0.14f, -0.05f, 0.11f, -0.09f, 0.04f
};
constexpr float kPadPanPattern[12] = {
    -0.42f, -0.18f, 0.08f, 0.32f, 0.55f, 0.72f,
     0.48f,  0.18f, -0.12f, -0.36f, -0.58f, -0.16f
};

// Note voice: sine osc + triangle companion (gain .10, freq x1.0014).
constexpr float kPadTriGain   = 0.10f;
constexpr float kPadTriDetune = 1.0014f;
// Pad base gain -4 dB; direct path .22, reverse path .92.
constexpr float kPadBaseDb  = -4.0f;
constexpr float kPadDirect  = 0.22f;
constexpr float kPadReverse = 0.92f;
// Nathan 2026-10-08: "pads are twice as loud as drone" — trim pads
// so they sit as warm body beside/behind the drone, not over it.
constexpr float kPadTrim = 0.38f;
// Vibrato: 4.9 Hz, depth 12 cents + breath-modulated 9 cents
// (breath = same 10.9 s cycle as the breath bed).
constexpr float kPadVibHz          = 4.9f;
constexpr float kPadVibDepthCents  = 12.0f;
constexpr float kPadBreathVibCents = 9.0f;

// Reverse pre-echo: triangle at target*.5, starts 2.35 s before note,
// lowpass 420->1240 Hz, env to .17, pan = -directPan*.82.
constexpr float kPadRevPreSec   = 2.35f;
constexpr float kPadRevLpStart  = 420.0f;
constexpr float kPadRevLpEnd    = 1240.0f;
constexpr float kPadRevEnvPeak  = 0.17f;
constexpr float kPadRevPanScale = -0.82f;

// Steel delay: 8 feed-forward taps at .5,1,...,4 s; feedback .65 through
// 720 Hz lowpass; wet .62.
constexpr int kSteelTaps = 8;
constexpr float kSteelTapSec[kSteelTaps] = {
    0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f
};
constexpr float kSteelTapLevel[kSteelTaps] = {
    0.32f, 0.285f, 0.25f, 0.22f, 0.195f, 0.17f, 0.15f, 0.13f
};
constexpr float kSteelTapPan[kSteelTaps] = {
    -0.96f, 0.96f, -0.88f, 0.88f, -0.98f, 0.98f, -0.92f, 0.92f
};
constexpr float kSteelFb     = 0.65f;
constexpr float kSteelFbLpHz = 720.0f;
constexpr float kSteelWet    = 0.62f;

// Reverse delay: 4 stages of .72 s; feedback .38 through 860 Hz lowpass;
// wet .42 -> space delay, .72 -> spaceIn.
constexpr int kRevStages = 4;
constexpr float kRevStageSec = 0.72f;
constexpr float kRevStageLevel[kRevStages] = { 0.10f, 0.20f, 0.36f, 0.57f };
constexpr float kRevStagePan = 0.94f; // pans alternate -/+
constexpr float kRevFb       = 0.38f;
constexpr float kRevFbLpHz   = 860.0f;
constexpr float kRevWetSpaceDelay = 0.42f;
constexpr float kRevWetSpaceIn    = 0.72f;

// ---------------------------------------------------------------------------
// 5. Beat patterns (electronic DUM/TAK/riq machine — NEVER a rock backbeat)
// ---------------------------------------------------------------------------
enum Bol : uint8_t {
    kBolDum = 0, kBolTak, kBolRiq,
    kBolDha, kBolGe, kBolNa, kBolTi, kBolKa, kBolDhi,
    kBolDhin, kBolTin, kBolTun, kBolKat, kBolTa,
    kBolTiRaKiTa, kBolDhaGe,
    kNumBols
};

enum BeatPatternId : int {
    kPatternBaladi = 0,
    kPatternMaqsum  = 1,
    kPatternMalfuf  = 2,
    kPatternKeherwa = 3,
    kPatternDadra   = 4,
    kPatternRupak   = 5,
    kPatternJhaptal = 6,
    kPatternEktaal  = 7,
    kPatternTeental = 8,
    kNumBeatPatterns = 9,
};

// Fixed-size index lists; only the first *Count entries are live.
struct BeatPattern {
    const char* name;
    uint8_t beats;
    uint8_t bols[16];
    uint8_t bassCount;  uint8_t bass[4];
    uint8_t bayanCount; uint8_t bayan[4];
    uint8_t dayanCount; uint8_t dayan[6];
    uint8_t handCount;  uint8_t hand[5];
};

constexpr BeatPattern kBeatPatterns[kNumBeatPatterns] = {
    { "Baladi", 8,
      { kBolDum,kBolDum,kBolRiq,kBolTak,kBolDum,kBolRiq,kBolTak,kBolRiq },
      3, { 0,1,4 }, 2, { 0,4 }, 2, { 3,6 }, 3, { 2,5,7 } },
    { "Maqsum", 8,
      { kBolDum,kBolTak,kBolRiq,kBolTak,kBolDum,kBolRiq,kBolTak,kBolRiq },
      2, { 0,4 }, 1, { 0 }, 3, { 1,3,6 }, 3, { 2,5,7 } },
    { "Malfuf", 2,
      { kBolDum,kBolTak },
      1, { 0 }, 1, { 0 }, 1, { 1 }, 1, { 1 } },
    { "Keherwa", 8,
      { kBolDha,kBolGe,kBolNa,kBolTi,kBolNa,kBolKa,kBolDhi,kBolNa },
      2, { 0,4 }, 3, { 0,1,6 }, 3, { 2,4,7 }, 2, { 3,5 } },
    { "Dadra", 6,
      { kBolDha,kBolDhin,kBolNa,kBolDha,kBolTin,kBolNa },
      2, { 0,3 }, 2, { 0,3 }, 4, { 1,2,4,5 }, 2, { 2,5 } },
    { "Rupak", 7,
      { kBolTin,kBolTin,kBolNa,kBolDhin,kBolNa,kBolDhin,kBolNa },
      2, { 3,5 }, 2, { 3,5 }, 4, { 0,2,4,6 }, 3, { 1,4,6 } },
    { "Jhaptal", 10,
      { kBolDhi,kBolNa,kBolDhi,kBolDhi,kBolNa,kBolTin,kBolNa,kBolDhi,kBolDhi,kBolNa },
      3, { 0,2,7 }, 3, { 0,3,7 }, 4, { 1,4,5,8 }, 4, { 1,4,6,9 } },
    { "Ektaal", 12,
      { kBolDhin,kBolDhin,kBolDhaGe,kBolTiRaKiTa,kBolTun,kBolNa,
        kBolKat,kBolTa,kBolDhaGe,kBolTiRaKiTa,kBolDhin,kBolNa },
      4, { 0,2,8,10 }, 2, { 0,8 }, 4, { 1,4,6,10 }, 5, { 3,5,7,9,11 } },
    { "Teental", 16,
      { kBolDha,kBolDhin,kBolDhin,kBolDha,kBolDha,kBolDhin,kBolDhin,kBolDha,
        kBolDha,kBolTin,kBolTin,kBolTa,kBolTa,kBolDhin,kBolDhin,kBolDha },
      3, { 0,4,12 }, 3, { 0,4,12 }, 6, { 2,6,9,10,13,15 }, 5, { 3,7,8,11,15 } },
};

constexpr int kDefaultBeatPattern = kPatternBaladi;

// ---------------------------------------------------------------------------
// 6. Tempo sources + speed
// ---------------------------------------------------------------------------
struct TempoSource {
    const char* name;
    float bpm;
};

constexpr TempoSource kTempoSources[5] = {
    { "Sun",      118.3f },
    { "Earth OM", 127.6f },
    { "Earth Day",121.4f },
    { "Moon",      98.6f },
    { "Schumann", 117.4f },
};

constexpr int kDefaultTempoSource = 2; // Earth Day

enum BeatSpeed : int {
    kSpeedFull    = 0,
    kSpeedHalf    = 1,
    kSpeedQuarter = 2,
};
constexpr float kSpeedScale[3] = { 1.0f, 0.5f, 0.25f };
constexpr int kDefaultBeatSpeed = kSpeedQuarter; // -> 30.35 BPM at Earth Day

// ---------------------------------------------------------------------------
// 7. Pulse (sparse pitch-dive toms)
// ---------------------------------------------------------------------------
constexpr int kPulseGraceCycles = 3; // full pattern cycles with NO toms
constexpr float kPulseSubRatio  = 0.5f;
constexpr float kPulseSubGain   = 0.13f;
constexpr float kPulseRootRatio = 1.0f;
constexpr float kPulseRootGain  = 0.115f;
constexpr float kPulseTomAttackSec = 0.065f;
constexpr float kPulseTomDiveFrac  = 0.72f; // dive completes over 72% of decay
constexpr float kPulseTomPan = 0.18f;       // -/+ (SUB left)
constexpr float kPulseFleckPeak = 0.0045f;
// Dub siren every 4th phrase.
constexpr float kSirenRatio   = 0.25f; // f/4
constexpr float kSirenDurSec  = 8.4f;
constexpr float kSirenStartRatio = 1.06f;
constexpr float kSirenGain    = 0.006f;
constexpr float kSirenEchoSec = 0.72f;
constexpr float kSirenEchoFb  = 0.31f;
constexpr float kSirenEchoWet = 0.13f;

// ---------------------------------------------------------------------------
// 8. Beat voice parameters (all tuned to the ACTIVE chakra via
//    foldFrequency: fold hz into [lo,hi] by octaves)
// ---------------------------------------------------------------------------
constexpr float kDumPitchStartRatio = 1.12f;
constexpr float kDumPitchSec        = 0.18f;
constexpr float kDumHarm2Gain      = 0.065f;
// Beat levels rebalanced 2026-10-08 per Nathan: bass drums (dum/kick/bayan)
// down, mid/high voices (tak/hand/shaker) up. Woodblock UNCHANGED.
constexpr float kDumLevelDown      = 0.18f;
constexpr float kDumLevel          = 0.15f;

constexpr float kKickStartHz = 76.0f;
constexpr float kKickMidHz   = 48.0f;
constexpr float kKickEndHz   = 38.0f;
constexpr float kKickPitchSec = 0.16f;
constexpr float kKickLpHz    = 170.0f;
constexpr float kKickLevelDown = 0.08f;
constexpr float kKickLevel     = 0.06f;

constexpr float kBayanPitchStartRatio = 1.28f;
constexpr float kBayanPitchSec        = 0.30f;
constexpr float kBayanSubGain  = 0.045f;
constexpr float kBayanLpHz    = 235.0f;
constexpr float kBayanDecaySec = 0.78f;
constexpr float kBayanLevelDown = 0.085f;
constexpr float kBayanLevel     = 0.065f;
constexpr float kBayanPan = -0.18f;

// TAK/woodblock (dayan positions, EVERY OTHER written occurrence):
// sine hz=fold(f*1.8,620,1050), 1.025x->hz over .075 s, bandpass at hz Q .72,
// env .022 attack -> .25 s decay, plus noise fleck bandpass 1120 Q .46 at
// .09x level. Feeds 6 ping-pong taps (NO FEEDBACK — feed-forward taps only).
constexpr float kTakPitchStartRatio = 1.025f;
constexpr float kTakPitchSec        = 0.075f;
constexpr float kTakBpQ      = 0.72f;
constexpr float kTakAttackSec = 0.022f;
constexpr float kTakDecaySec  = 0.25f;
constexpr float kTakLevelDown = 0.12f;
constexpr float kTakLevel     = 0.105f;
constexpr float kTakPan       = 0.24f;
constexpr float kTakFleckBpHz = 1120.0f;
constexpr float kTakFleckBpQ  = 0.46f;
constexpr float kTakFleckGain = 0.09f;

constexpr int kWoodTaps = 6; // eighth-note spacing, recomputed on tempo change
constexpr float kWoodTapLevel[kWoodTaps] = { 0.125f, 0.086f, 0.059f, 0.040f, 0.027f, 0.018f };
constexpr float kWoodTapPan[kWoodTaps]   = { -0.72f, 0.72f, -0.68f, 0.68f, -0.64f, 0.64f };

// Hand (riq+shaker): frame drum + jingle noise + shaker noise, pans alternate.
constexpr float kHandPitchStartRatio = 1.12f;
constexpr float kHandLpHz     = 460.0f;
constexpr float kHandDecaySec = 0.32f;
constexpr float kHandLevel    = 0.10f;
constexpr float kHandJingleHpHz = 1450.0f;
constexpr float kHandJingleLpHz = 3800.0f;
constexpr float kHandJingleGain = 0.2f; // x level
constexpr float kHandShakerHpHz = 240.0f;
constexpr float kHandShakerDecaySec = 0.34f;
constexpr float kHandShakerLevel = 0.024f;

// Drum bus: 4-beat pan orbit depth .38; lowpass 2600 -> tanh(1.25x)/tanh(1.25)
// waveshaper (2x) -> compressor (thr -5 dB, knee 5, ratio 2.2, att .008,
// rel .075).
constexpr float kDrumOrbitDepth = 0.38f;
constexpr float kDrumLpHz = 2600.0f;
constexpr float kDrumDrive = 1.25f;
constexpr float kDrumCompThrDb = -5.0f;
constexpr float kDrumCompKneeDb = 5.0f;
constexpr float kDrumCompRatio = 2.2f;
constexpr float kDrumCompAttSec = 0.008f;
constexpr float kDrumCompRelSec = 0.075f;

// ---------------------------------------------------------------------------
// 9. Space
// ---------------------------------------------------------------------------
constexpr float kSpaceIrSeconds = 30.0f; // IR = noise * (1-t)^0.68
constexpr float kSpaceIrDecayExp = 0.68f;
// Mid/side: mid gains .42/.42, side 1.20/-1.20 (240% width).
constexpr float kSpaceMidGain  = 0.42f;
constexpr float kSpaceSideGain = 1.20f;
constexpr float kSpaceDecorrL = 0.013f;
constexpr float kSpaceDecorrR = 0.067f;
// Returns scale with SPACE fader: reverb x.58, delay x.50.
constexpr float kSpaceReverbReturn = 0.58f;
constexpr float kSpaceDelayReturn  = 0.50f;
// Return EQ: highpass 250 (Q .62) -> peaking -9 dB @720 (Q .48) ->
// peaking -5.5 dB @1550 (Q .78) -> highshelf +1.5 dB @4800.
constexpr float kSpaceEqHpFreq = 250.0f;
constexpr float kSpaceEqHpQ    = 0.62f;
constexpr float kSpaceEqPk1Freq = 720.0f;
constexpr float kSpaceEqPk1Q    = 0.48f;
constexpr float kSpaceEqPk1Db   = -9.0f;
constexpr float kSpaceEqPk2Freq = 1550.0f;
constexpr float kSpaceEqPk2Q    = 0.78f;
constexpr float kSpaceEqPk2Db   = -5.5f;
constexpr float kSpaceEqShelfFreq = 4800.0f;
constexpr float kSpaceEqShelfDb   = 1.5f;
// Drone delay: .56 s, lowpass 760, in .34, out .24 -> space delay return.
constexpr float kDroneDelaySec = 0.56f;
constexpr float kDroneDelayLpHz = 760.0f;
constexpr float kDroneDelayIn   = 0.34f;
constexpr float kDroneDelayOut  = 0.24f;
// delayBloom .46: space delay return -> spaceIn (reverb input).
constexpr float kDelayBloom = 0.46f;

// ---------------------------------------------------------------------------
// 10. Master routing & defaults
// ---------------------------------------------------------------------------
// breathBed: base .78, depth .22, 10.9 s cycle (4.4 s inhale smoothstep up,
// 6.5 s exhale smoothstep down, no plateau) -> master.
constexpr float kBreathBase   = 0.78f;
constexpr float kBreathDepth  = 0.22f;
constexpr float kBreathCycleSec  = 10.9f;
constexpr float kBreathInhaleSec = 4.4f;
constexpr float kBreathExhaleSec = 6.5f;
// felt bass: 40 Hz sine -> lowpass 88 -> gain .024 -> master (always on).
constexpr float kFeltFreq = 40.0f;
constexpr float kFeltLpHz = 88.0f;
constexpr float kFeltGain = 0.007f; // Nathan 2026-10-08: felt sub was as loud as
// the drone; reduced ~10.7 dB to a subtle floor (was 0.024).
// Web master gain: MASTER=0.62 * 0.9 = 0.558 (web: fx.master.gain set to
// MASTER*0.9 when sound is on). The native port omitted this, running ~5 dB
// hot vs the web app.
constexpr float kMasterGain = 0.558f;
// NO compressor on drone/pads path. NO noise/hiss source anywhere.
constexpr float kDefaultFaderDrone = 1.00f;
constexpr float kDefaultFaderPulse = 0.69f;
constexpr float kDefaultFaderPads  = 0.32f;
constexpr float kDefaultFaderBeat  = 0.92f;
constexpr float kDefaultFaderSpace = 0.68f;

// ---------------------------------------------------------------------------
// Utility DSP blocks (method declarations only; defined in Sg9Dsp.cpp)
// ---------------------------------------------------------------------------

// Deterministic PRNG (splitmix32). No std::rand, no wall clock.
struct SeededRng {
    explicit SeededRng(uint32_t seed = 0x9E3779B9u);
    void seed(uint32_t s);
    uint32_t next();                 // [0, 2^32)
    float nextFloat();               // [0, 1)
    float nextRange(float a, float b);// [a, b)
    uint32_t state;
};

// Periodic wavetable built from Fourier sine-phase coefficients.
struct Wavetable {
    void build(const float* coeffs, int numCoeffs, int size);
    float sample(float phase01) const; // linear-interpolated, phase in [0,1)
    std::vector<float> data;           // preallocated in build()
    int size = 0;
};

// Sine-sum wavetable oscillator with detune in cents.
struct WavetableOsc {
    void setTable(const Wavetable* t);
    void setSampleRate(double sr);
    void setFreq(float hz);
    void setDetuneCents(float cents);
    void reset(float phase01 = 0.0f);
    float tick(); // advance phase, return interpolated sample
    const Wavetable* table = nullptr;
    double sampleRate = 48000.0;
    float freq = 440.0f;
    float detuneCents = 0.0f;
    float phase = 0.0f; // [0,1)
};

// RBJ cookbook biquad (lowpass/highpass/bandpass/peaking/highshelf/allpass).
struct Biquad {
    void setSampleRate(double sr);
    void setLowpass(float freq, float q);
    void setHighpass(float freq, float q);
    void setBandpass(float freq, float q); // constant-skirt
    void setPeaking(float freq, float q, float gainDb);
    void setHighshelf(float freq, float gainDb);
    void setAllpass(float freq, float q);
    void reset();
    float process(float x);
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
    double sampleRate = 48000.0;
};

// Fractional delay line. Buffer preallocated in prepare(); no allocation
// in the hot path.
struct DelayLine {
    void prepare(double sampleRate, float maxSeconds);
    void reset();
    void setDelaySeconds(float d);
    void write(float x);
    float read() const;                 // at current delay (fractional)
    float readAtSeconds(float d) const; // arbitrary tap (fractional)
    std::vector<float> buf;             // preallocated ring buffer
    int size = 0;
    int writePos = 0;
    double sampleRate = 48000.0;
    float delaySamples = 0.0f;
};

// One-pole parameter smoother (exponential approach to target).
struct ParamSmooth {
    void setSampleRate(double sr);
    void setTimeConstant(float seconds);
    void reset(float v);
    void setTarget(float v);
    float tick();
    float value = 0.0f;
    float target = 0.0f;
    float coeff = 1.0f;
    double sr_ = 48000.0;
};

// Non-uniform (Gardner) partitioned convolver for the 30 s space IR.
// The IR is processed at half the host rate (convRate_ = sampleRate/2;
// a 24 kHz internal rate is sonically transparent for a reverb tail and
// halves memory/CPU). Partitions start at baseBlock_ conv-rate samples and
// double in pairs; each partition's FFT is 2*size. setIR() takes a
// host-rate stereo IR and decimates it internally with the same 64-tap
// prototype used by the runtime resamplers. processBlock() operates at the
// CONV rate (callers resample); it stages arbitrary input lengths to
// baseBlock_ multiples.
struct PartitionedConvolver {
    void prepare(double sampleRate, float irSeconds, int blockSize);
    void reset(); // clears state; the IR partitions are preserved
    // Load a stereo IR (numSamples per channel, HOST rate); converts to
    // freq-domain partitions internally (decimated 2x).
    void setIR(const float* irL, const float* irR, int numSamples);
    // Process n CONV-rate samples; zero-pads/stages internally.
    void processBlock(const float* inL, const float* inR,
                      float* outL, float* outR, int n);
    int blockSize() const;      // conv-rate base block (128)
    int latencySamples() const; // ~host-rate samples of total wet latency
    static void fft(float* re, float* im, int n, bool inverse);

    struct Partition {
        int size = 0;   // Sp, conv-rate samples (pow2 multiple of baseBlock_)
        int offset = 0; // Op, conv-rate samples (cumulative IR offset)
        int fftLen = 0; // 2*size
        std::vector<float> irReL, irImL, irReR, irImR; // fftLen each
        long long windowsDone = 0; // input windows already transformed
        int slot = 0;   // stagger offset (ticks) within one period
    };
    void convolverTick(); // one baseBlock_ of conv-rate input in inStage_
    void buildDeciTaps();
    // 2x resampling around the conv-rate core (host <-> conv rate).
    // decimate returns the number of conv-rate samples written.
    int decimate(const float* in, float* out, int n, int ch);
    int interpolate(const float* in, float* out, int n, int ch);
    void processCore(const float* inL, const float* inR,
                     float* outL, float* outR, int m); // conv rate

    std::vector<Partition> parts_;
    int baseBlock_ = 128;
    double convRate_ = 24000.0;
    double sampleRate_ = 48000.0;
    std::vector<float> outBufL_, outBufR_; // circular, conv-rate samples
    int outLen_ = 0;
    long long totalIn_ = 0;
    std::vector<float> inStageL_, inStageR_; // baseBlock_ staging
    std::vector<float> outStageL_, outStageR_;
    int inFill_ = 0;
    int outPos_ = 0;
    bool outStaged_ = false;
    std::vector<float> tmpRe_, tmpIm_, accRe_, accIm_; // fft temps (max fftLen)
    std::vector<float> deciTaps_; // 64-tap 2x decimation prototype (host rate)
    // Input history (circular, conv-rate) for just-in-time partition FFTs.
    // Sized to cover the maximum partition offset plus one max window.
    std::vector<float> histL_, histR_;
    int histSize_ = 0;
    int histPos_ = 0; // write position (oldest sample just before it)
    // Runtime resampler state (each history has its own cursor).
    std::vector<float> rsDeciHistL_, rsDeciHistR_;
    std::vector<float> rsInterHistL_, rsInterHistR_;
    int rsDeciPosL_ = 0, rsDeciPosR_ = 0, rsInterPosL_ = 0, rsInterPosR_ = 0;
    int rsPhL_ = 0, rsPhR_ = 0;
    std::vector<float> cvInL_, cvInR_, cvOutL_, cvOutR_; // conv-rate scratch
};

// ---------------------------------------------------------------------------
// Parameter block (plain-old-data; the host/JUCE layer owns one of these)
// ---------------------------------------------------------------------------
struct Sg9Params {
    int voiceLevels[kNumChakras] = { 55, 48, 52, 60, 68, 58, 62, 42, 46 };
    float faderDrone = kDefaultFaderDrone; // 1.00
    float faderPulse = kDefaultFaderPulse; // 0.69
    float faderPads  = kDefaultFaderPads;  // 0.32
    float faderBeat  = kDefaultFaderBeat;  // 0.92
    float faderSpace = kDefaultFaderSpace; // 0.68
    int droneTone   = kDefaultDroneTone;   // 4 = paSaSa
    int tempoSource = kDefaultTempoSource; // 2 = Earth Day
    int padVoicing  = kDefaultPadVoicing;  // 6 = majorThird
    int beatSpeed   = kDefaultBeatSpeed;    // 0=full 1=half 2=quarter
    int beatPattern = kDefaultBeatPattern; // 0 = baladi
    bool pulseOn = true;
    float masterFader = 1.0f; // user master output trim (1.0 = unity = web 0.558 level)
};

// ---------------------------------------------------------------------------
// Sg9Dsp — the full web-engine port. Declarations only; see Sg9Dsp.cpp.
// ---------------------------------------------------------------------------
class Sg9Dsp {
public:
    Sg9Dsp();

    // --- host-facing interface (must stay source-compatible with the
    // --- JUCE wrapper in ../juce/Sg9Processor.cpp) ---
    void prepare(double sampleRate);
    void reset();
    void setParams(const Sg9Params& p);
    void process(float* outL, float* outR, int numSamples); // synth: no input
    void triggerVoice(int index);   // 0..8; tapping active voice stops all
    void releaseVoice(int index);   // release voice index (or all if < 0)
    void noteOn(int midiNote, float velocity); // -> nearest chakra trigger
    void noteOff(int midiNote);
    int getActiveTrigger() const;   // -1 = none sounding
    int getLatencySamples() const;  // partitioned-convolver latency
    float getOutMeterL() const;
    float getOutMeterR() const;
    float getVoiceLevel(int voice) const; // smoothed 0..1 for editor glow
    // --- test seams (no audio-path impact; default behavior unchanged) ---
    float getDronePreGain() const { return dronePreGain_; }
    void debugSetDronePreGain(float g) { dronePreGain_ = g; }
    // Post-envelope drone bus RMS (most recent block, pre-fader, excludes
    // felt bass). Test seam for verifying the pre-gain stage.
    float getDroneBusRms() const { return droneBusRms_; }

private:
    struct BeatHit; // defined below with the state members
    struct WoodTap; // defined below with the state members
    // --- per-section renderers (accumulate into the bus vectors) ---
    void renderDrone(float* busL, float* busR, int n);
    void renderDroneReeds(float* busL, float* busR, int n);
    void renderDroneJawari(float* busL, float* busR, int n);
    void renderDroneColorReeds(float* busL, float* busR, int n);
    void renderPads(float* busL, float* busR, int n);
    void renderPadNotes(float* busL, float* busR, int n);
    void renderReversePreEcho(float* busL, float* busR, int n);
    void renderPadDelays(float* busL, float* busR, int n); // steel + reverse
    void renderPulseBeat(float* pulseL, float* pulseR,
                         float* drumL, float* drumR, int n);
    void renderTom(float* busL, float* busR, int n);
    void renderDubSiren(float* busL, float* busR, int n);
    void renderBeatHits(float* busL, float* busR, int n);
    void renderWoodTaps(float* busL, float* busR, int n);
    void renderDroneDelayToSpace(float* spaceDelayL, float* spaceDelayR, int n);
    void renderSpace(float* spaceSendL, float* spaceSendR,
                     float* spaceDelayL, float* spaceDelayR,
                     float* mixL, float* mixR, int n);
    void renderMaster(float* mixL, float* mixR, int n); // breath, felt, tanh, meters

    // --- helpers ---
    void updateVoiceEnvelope(int n);
    void configureDroneForVoice(int voiceIdx, bool resetPhases = true);
    void updateDroneDrift(float dt);
    void startPadStep();
    void restartPadPhrase(); // chunk C: pads (+pulse grace) follow the chakra
    void advanceScheduler(int n);
    void onBeat(int beatInPattern, bool patternStart, float delaySec = 0.0f);
    void triggerTom(bool sub, float delaySec = 0.0f);
    void triggerDubSiren(float delaySec = 0.0f);
    void fireBeatHit(int kind, float gain, float pan, bool downbeat,
                      float delaySec = 0.0f);
    float beatHitSample(BeatHit& h, float t, float shakerLpAlpha, float hpAlpha);
    void firePatternBeat(const BeatPattern& pat, int beat, bool downbeat,
                         float delaySec);
    void renderFeltBass(float* mixL, float* mixR, int n);
    void drumBusChain(float* busL, float* busR, int n);
    void buildSpaceIR(); // 30 s stereo noise IR x (1-t)^.68 -> convolver
    void scheduleWoodTaps();
    float getBpm() const;
    float foldFrequency(float hz, float lo, float hi) const;
    static float midiToFreq(int midi);
    int nearestChakra(float freq) const;
    void buildPadCurves();
    void clearBuses(int n);

    // ================= master / host state =================
    double sampleRate_ = 48000.0;
    double invSampleRate_ = 1.0 / 48000.0;
    Sg9Params params_;
    SeededRng rng_{ 0x51ab3d21u };

    int activeVoice_ = -1;
    int prevVoice_ = -1;          // releasing voice during 1.5 s handoff
    float voiceEnv_ = 0.0f;       // monophonic voice envelope 0..1
    float voicePeak_ = 0.0f;      // 0.22*(level/100)^1.45 for active voice
    float prevVoiceEnv_ = 0.0f;   // release tail of previous voice
    float attackT_ = 0.0f;        // seconds since trigger (attack phase)
    float relT_ = 0.0f;           // seconds since release began
    float relStartEnv_ = 0.0f;    // envelope level at release start
    bool attacking_ = false;
    bool releasing_ = false;
    float voiceLevelSm_[kNumChakras] = {}; // smoothed levels for editor

    float outMeterL_ = 0.0f;
    float outMeterR_ = 0.0f;

    // Preallocated mix buses (sized in prepare to maxBlock; zeroed per block).
    int maxBlock_ = 0;
    std::vector<float> busDroneL_, busDroneR_;
    std::vector<float> busPadsL_, busPadsR_;
    std::vector<float> busPulseL_, busPulseR_;
    std::vector<float> busDrumsL_, busDrumsR_;
    std::vector<float> busSpaceSendL_, busSpaceSendR_;
    std::vector<float> busSpaceDelayL_, busSpaceDelayR_;
    std::vector<float> busMixL_, busMixR_;
    std::vector<float> convInL_, convInR_;   // convolver staging
    std::vector<float> convOutL_, convOutR_;
    int convBuffered_ = 0;

    // ================= drone state =================
    Wavetable reedTable_;       // built from kReedWave
    Wavetable majorReedTable_;  // built from kMajorReedWave
    Wavetable jawariTable_;     // built from kJawariWave
    static constexpr int kWavetableSize = 2048;

    // Core reed pairs: core (2), left (2), right (2).
    WavetableOsc droneCore_[2];
    WavetableOsc droneLeft_[2];
    WavetableOsc droneRight_[2];
    // Octave doublings: sL, sR, h2, h3 (pairs).
    WavetableOsc droneOctSL_[2];
    WavetableOsc droneOctSR_[2];
    WavetableOsc droneOctH2_[2];
    WavetableOsc droneOctH3_[2];
    // Jawari shimmer oscs (3).
    WavetableOsc droneJawari_[3];
    // Harmony-follow color reeds: stereo pairs with asymmetric gains.
    WavetableOsc droneMinorLo_[2];
    WavetableOsc droneMinorHi_[2];
    WavetableOsc droneMajorLo_[2];
    WavetableOsc droneMajorHi_[2];
    ParamSmooth minorFollow_; // 0/1 smoothed over kHarmonySmoothSec
    ParamSmooth majorFollow_;

    Biquad droneBodyLpL_, droneBodyLpR_;
    float droneCutoff_ = 2000.0f; // eases baseCut*.76 -> baseCut
    float droneCutTarget_ = 2000.0f;
    Biquad droneJawHpL_, droneJawHpR_;
    Biquad droneJawLpL_, droneJawLpR_;
    Biquad droneApL_[3], droneApR_[3]; // all-pass drift chain
    float apLfoPhase_[3] = {};
    float orbitPhase_ = 0.0f;
    float bellowsPhase_ = 0.0f, bellowsPhase2_ = 1.7f; // hand-pump LFOs
    // Micro-delay widener: 9-sample delay on drone R channel for natural
    // stereo width (0.19 ms — below echo threshold, just spatial).
    static constexpr int kWideDelayN = 9;
    float wideBuf_[kWideDelayN] = {};
    int widePos_ = 0;

    // Slow drift tracks.
    float driftClock_ = 0.0f;
    float driftNextRetrig_ = kDriftStartSec;
    float driftLastRetrig_ = 0.0f;
    float droneBaseCut_ = 2000.0f; // baseCut at configure time (drift base)
    float droneBaseHz_ = 417.0f;
    float dronePreGain_ = kDronePreGain; // test seam may override
    float droneBusRms_ = 0.0f; // test seam: post-envelope drone bus RMS
    float driftFilter_ = 1.0f, driftFilterT_ = 1.0f, driftFilterS_ = 1.0f;
    float driftCore_ = 1.0f,   driftCoreT_ = 1.0f,   driftCoreS_ = 1.0f;
    float driftDetune_ = 1.9f, driftDetuneT_ = 1.9f, driftDetuneS_ = 1.9f; // cents
    float driftJawari_ = 1.0f, driftJawariT_ = 1.0f, driftJawariS_ = 1.0f;
    float driftH3_ = 1.0f,     driftH3T_ = 1.0f,     driftH3S_ = 1.0f;

    DelayLine droneDelay_; // .56 s, lp 760, in .34 / out .24 -> space delay
    Biquad droneDelayLp_;

    // ================= pad state =================
    struct PadNote {
        bool active = false;
        WavetableOsc osc;   // sine
        WavetableOsc tri;   // triangle companion, gain .10, x1.0014
        float freq = 0.0f;        // current (gliding) freq
        float fromFreq = 0.0f;
        float targetFreq = 0.0f;
        float glideT = 0.0f;      // 0..1 over kPadGlideSec
        float ageSec = 0.0f;      // time since note start
        float pan = 0.0f;
        float vibPhase = 0.0f;
        int stepIndex = 0;
        Biquad lp;
        float lpCut_ = 2000.0f;
        bool killing_ = false; // chunk C: quick fade on voicing retrigger
        float killT_ = 0.0f;
    };
    static constexpr int kMaxPadNotes = 4;
    PadNote padNotes_[kMaxPadNotes];
    int padStep_ = 0;
    float padStepClock_ = 0.0f; // seconds into current 6 s step
    float padSwell_[kPadSwellPts];
    float padRelease_[kPadReleasePts];
    float breathPhase_ = 0.0f;  // shared 10.9 s breath cycle
    float padPrevFreq_ = 0.0f;  // chunk C: glide source (prev step target)
    int padVoiceIdx_ = -99;     // chunk C: retrigger detect (active chakra)
    int padVoicingIdx_ = -99;   // chunk C: retrigger detect (pad voicing)

    // Reverse pre-echo voice.
    bool revPreActive_ = false;
    float revPreT_ = 0.0f;
    float revPrePan_ = 0.0f;
    float revPreArrival_ = 0.0f; // chunk C: detune sweep target (cents)
    WavetableOsc revPreOsc_;
    Biquad revPreLp_;
    Wavetable sineTable_; // shared sine for pads/toms/kicks
    Wavetable triTable_;  // shared triangle for pad companions/pre-echo

    // Steel delay: 8 feed-forward taps + .65 feedback through 720 Hz LP.
    DelayLine steelDelay_; // >= 4 s
    Biquad steelTapLp_[kSteelTaps];
    Biquad steelFbLp_;
    // Reverse delay: 4 stages of .72 s, .38 feedback through 860 Hz LP.
    DelayLine revDelay_; // >= 2.88 s
    Biquad revFbLp_;

    // ================= pulse / beat scheduler state =================
    float bpm_ = 121.4f * 0.25f;
    float beatSec_ = 60.0f / 30.35f;
    float eighthSec_ = 0.0f;
    float schedClock_ = 0.0f;  // seconds since trigger (drives beats)
    float nextBeatAt_ = 0.0f;  // scheduler time of next beat
    int beatInPattern_ = 0;
    int patternCycle_ = 0;     // completed full pattern cycles
    int pulsePhrase_ = 0;      // alternates SUB / ROOT
    bool dayanAlt_ = false;    // woodblock: every other written occurrence

    // Pitch-dive tom (monophonic).
    bool tomActive_ = false;
    float tomT_ = 0.0f;
    float tomDecay_ = 2.55f;
    float tomStartFreq_ = 440.0f;
    float tomTargetFreq_ = 220.0f;
    float tomGain_ = 0.115f;
    float tomPan_ = 0.18f;
    WavetableOsc tomBody_;
    WavetableOsc tomSub_;
    Biquad tomLp_;
    float tomFleckPhase_ = 0.0f; // muted noise fleck LFO phase

    // Dub siren (every 4th phrase).
    bool sirenActive_ = false;
    float sirenT_ = 0.0f;
    float sirenTarget_ = 0.0f; // chunk C: captured f/4 at trigger time
    WavetableOsc sirenOsc_;
    Biquad sirenLp_;
    DelayLine sirenDly_; // .72 s dark echo
    Biquad sirenEchoLp_;

    // One-shot beat hits (DUM / analog kick / bayan / TAK / hand).
    enum BeatHitKind : uint8_t {
        kHitDum = 0, kHitKick, kHitBayan, kHitTak, kHitHand
    };
    struct BeatHit {
        bool active = false;
        uint8_t kind = 0;
        float t = 0.0f;       // seconds since hit start (negative = pre-delay)
        float dur = 1.0f;
        float gain = 0.0f;
        float pan = 0.0f;
        float freq = 0.0f;    // start freq (pitch env -> freqEnd)
        float freqEnd = 0.0f;
        float pitchSec = 0.2f;
        WavetableOsc oscA;
        WavetableOsc oscB;    // 2nd harmonic / sub / companion
        Biquad filt;
        Biquad filt2;         // second noise-filter stage (TAK fleck, hand)
        uint32_t nzState = 1; // xorshift white-noise state (deterministic)
        float st1 = 0.0f, st2 = 0.0f, st3 = 0.0f; // one-pole scratch states
        float noisePhase = 0.0f; // (legacy; unused by chunk D)
    };
    static constexpr int kMaxBeatHits = 24;
    BeatHit beatHits_[kMaxBeatHits];

    // Woodblock ping-pong taps: 6 feed-forward taps, NO feedback.
    struct WoodTap {
        bool active = false;
        float delayLeft = 0.0f; // seconds until this tap fires
        int tapIdx = 0;
        float freq = 0.0f;   // TAK pitch captured at schedule time
        float t = 0.0f;      // seconds since this tap fired
        bool sounding = false;
    };
    WoodTap woodTaps_[kWoodTaps];
    WavetableOsc woodOsc_[kWoodTaps];
    Biquad woodLp_[kWoodTaps];

    float beatOrbitPhase_ = 0.0f; // 4-beat pan orbit, depth .38
    Biquad drumToneLp_;           // 2600 Hz before waveshaper (L)
    Biquad drumToneLpR_;          // 2600 Hz before waveshaper (R)
    float drumCompEnv_ = 0.0f;    // drum-bus compressor follower

    // Drone delay second channel (stereo): .56 s, LP 760.
    DelayLine droneDelayR_;
    Biquad droneDelayLpR_;

    // Felt bass (always on): 40 Hz sine -> LP 88 -> .024.
    WavetableOsc feltOsc_;
    Biquad feltLp_;

    // ================= space state =================
    PartitionedConvolver convolver_;
    static constexpr int kConvBlockSize = 1024;
    Biquad spaceEqL_[4], spaceEqR_[4]; // HP, pk1, pk2, highshelf
    DelayLine decorrL_; // .013 s
    DelayLine decorrR_; // .067 s
    float bloomL_ = 0.0f; // delayBloom .46 feedback state (prev return)
    float bloomR_ = 0.0f;
};

} // namespace sg9
