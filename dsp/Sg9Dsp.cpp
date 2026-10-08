// SG-9 — framework-free C++17 DSP core (web-engine port), chunk B:
// utilities + drone voice. (Pads/pulse/beat/space/master/process glue are
// implemented by later chunks against the same header.)

#include "Sg9Dsp.h"

namespace sg9 {

// ---------------------------------------------------------------------------
// SeededRng (splitmix32)
// ---------------------------------------------------------------------------
SeededRng::SeededRng(uint32_t seed) : state(seed) {}
void SeededRng::seed(uint32_t s) { state = s; }
uint32_t SeededRng::next() {
    uint32_t z = (state += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x21f0aaadu;
    z = (z ^ (z >> 15)) * 0x735a2d97u;
    return z ^ (z >> 15);
}
float SeededRng::nextFloat() { return (next() >> 8) * (1.0f / 16777216.0f); }
float SeededRng::nextRange(float a, float b) { return a + (b - a) * nextFloat(); }

// ---------------------------------------------------------------------------
// Wavetable (Fourier sine-phase coefficients)
// ---------------------------------------------------------------------------
void Wavetable::build(const float* coeffs, int numCoeffs, int size) {
    data.assign((size_t)size, 0.0f);
    this->size = size;
    // Sum sines: x[i] = sum_k coeffs[k] * sin(2*pi*k*i/size).
    for (int i = 0; i < size; ++i) {
        double acc = 0.0;
        const double ph = 2.0 * kPi * i / size;
        for (int k = 1; k < numCoeffs; ++k) {
            const float c = coeffs[k];
            if (c != 0.0f) acc += c * std::sin(k * ph);
        }
        data[(size_t)i] = (float)acc;
    }
    // Peak-normalize to 1.0 (matches Web Audio PeriodicWave with
    // disableNormalization:false, as used by the web engine).
    float peak = 0.0f;
    for (float v : data) peak = std::max(peak, std::fabs(v));
    if (peak > 1e-6f) {
        const float s = 1.0f / peak;
        for (float& v : data) v *= s;
    }
}
float Wavetable::sample(float phase01) const {
    if (size <= 0 || data.empty()) return 0.0f;
    float p = phase01 - std::floor(phase01);
    const float idx = p * (float)size;
    const int i0 = (int)idx;
    const int i1 = (i0 + 1) % size;
    const float frac = idx - (float)i0;
    return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
}

// ---------------------------------------------------------------------------
// WavetableOsc
// ---------------------------------------------------------------------------
void WavetableOsc::setTable(const Wavetable* t) { table = t; }
void WavetableOsc::setSampleRate(double sr) { sampleRate = sr; }
void WavetableOsc::setFreq(float hz) { freq = hz; }
void WavetableOsc::setDetuneCents(float cents) { detuneCents = cents; }
void WavetableOsc::reset(float phase01) {
    phase = phase01 - std::floor(phase01);
    if (phase < 0.0f) phase += 1.0f;
}
float WavetableOsc::tick() {
    if (!table) return 0.0f;
    const float out = table->sample(phase);
    const double fEff = (double)freq * std::pow(2.0, detuneCents / 1200.0);
    phase += (float)(fEff / sampleRate);
    if (phase >= 1.0f) phase -= 1.0f;
    return out;
}

// ---------------------------------------------------------------------------
// Biquad (RBJ cookbook)
// ---------------------------------------------------------------------------
void Biquad::setSampleRate(double sr) { sampleRate = sr; }

static void biquadSet(Biquad& q, float b0, float b1, float b2,
                      float a0, float a1, float a2) {
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0;
    q.a1 = a1 / a0; q.a2 = a2 / a0;
}

void Biquad::setLowpass(float freq, float qv) {
    const double w = 2.0 * kPi * freq / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double a = s / (2.0 * qv);
    biquadSet(*this, (float)(1.0 - c) * 0.5f, (float)(1.0 - c), (float)(1.0 - c) * 0.5f,
              (float)(1.0 + a), (float)(-2.0 * c), (float)(1.0 - a));
}
void Biquad::setHighpass(float freq, float qv) {
    const double w = 2.0 * kPi * freq / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double a = s / (2.0 * qv);
    biquadSet(*this, (float)(1.0 + c) * 0.5f, (float)(-(1.0 + c)), (float)(1.0 + c) * 0.5f,
              (float)(1.0 + a), (float)(-2.0 * c), (float)(1.0 - a));
}
void Biquad::setBandpass(float freq, float qv) { // constant skirt gain
    const double w = 2.0 * kPi * freq / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double a = s / (2.0 * qv);
    biquadSet(*this, (float)a, 0.0f, (float)-a,
              (float)(1.0 + a), (float)(-2.0 * c), (float)(1.0 - a));
}
void Biquad::setPeaking(float freq, float qv, float gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * freq / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double a = s / (2.0 * qv);
    biquadSet(*this, (float)(1.0 + a * A), (float)(-2.0 * c), (float)(1.0 - a * A),
              (float)(1.0 + a / A), (float)(-2.0 * c), (float)(1.0 - a / A));
}
void Biquad::setHighshelf(float freq, float gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * freq / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double a = s / 2.0 * std::sqrt(A);
    const double sqA = std::sqrt(A);
    biquadSet(*this,
              (float)(A * ((A + 1.0) + (A - 1.0) * c + 2.0 * sqA * a)),
              (float)(-2.0 * A * ((A - 1.0) + (A + 1.0) * c)),
              (float)(A * ((A + 1.0) + (A - 1.0) * c - 2.0 * sqA * a)),
              (float)((A + 1.0) - (A - 1.0) * c + 2.0 * sqA * a),
              (float)(2.0 * ((A - 1.0) - (A + 1.0) * c)),
              (float)((A + 1.0) - (A - 1.0) * c - 2.0 * sqA * a));
}
void Biquad::setAllpass(float freq, float qv) {
    const double w = 2.0 * kPi * freq / sampleRate;
    const double c = std::cos(w), s = std::sin(w);
    const double a = s / (2.0 * qv);
    biquadSet(*this, (float)(1.0 - a), (float)(-2.0 * c), (float)(1.0 + a),
              (float)(1.0 + a), (float)(-2.0 * c), (float)(1.0 - a));
}
void Biquad::reset() { z1 = z2 = 0.0f; }
float Biquad::process(float x) {
    // Transposed Direct Form II; denormal guard keeps feedback loops safe.
    float xn = x + kDenormalNudge;
    const float y = b0 * xn + z1;
    z1 = b1 * xn - a1 * y + z2;
    z2 = b2 * xn - a2 * y;
    return y;
}

// ---------------------------------------------------------------------------
// DelayLine (fractional, linear interpolation)
// ---------------------------------------------------------------------------
void DelayLine::prepare(double sr, float maxSeconds) {
    sampleRate = sr;
    size = (int)std::ceil(maxSeconds * sr) + 4;
    buf.assign((size_t)size, 0.0f);
    writePos = 0;
    delaySamples = 0.0f;
}
void DelayLine::reset() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    writePos = 0;
}
void DelayLine::setDelaySeconds(float d) {
    delaySamples = (float)(d * sampleRate);
    if (delaySamples < 0.0f) delaySamples = 0.0f;
    if (delaySamples > (float)(size - 2)) delaySamples = (float)(size - 2);
}
void DelayLine::write(float x) {
    buf[(size_t)writePos] = x;
    if (++writePos >= size) writePos = 0;
}
float DelayLine::readAtSeconds(float d) const {
    float ds = (float)(d * sampleRate);
    if (ds < 0.0f) ds = 0.0f;
    if (ds > (float)(size - 2)) ds = (float)(size - 2);
    float rp = (float)writePos - ds;
    while (rp < 0.0f) rp += size;
    while (rp >= (float)size) rp -= size;
    const int i0 = (int)rp;
    const int i1 = (i0 + 1) % size;
    const float frac = rp - (float)i0;
    return buf[(size_t)i0] + (buf[(size_t)i1] - buf[(size_t)i0]) * frac;
}
float DelayLine::read() const {
    float rp = (float)writePos - delaySamples;
    while (rp < 0.0f) rp += size;
    while (rp >= (float)size) rp -= size;
    const int i0 = (int)rp;
    const int i1 = (i0 + 1) % size;
    const float frac = rp - (float)i0;
    return buf[(size_t)i0] + (buf[(size_t)i1] - buf[(size_t)i0]) * frac;
}

// ---------------------------------------------------------------------------
// ParamSmooth (one-pole)
// ---------------------------------------------------------------------------
void ParamSmooth::setSampleRate(double sr) { sr_ = sr; }
void ParamSmooth::setTimeConstant(float seconds) {
    const float t = seconds > 1e-4f ? seconds : 1e-4f;
    coeff = 1.0f - std::exp(-1.0f / (float)(t * sr_));
}
void ParamSmooth::reset(float v) { value = target = v; }
void ParamSmooth::setTarget(float v) { target = v; }
float ParamSmooth::tick() {
    value += (target - value) * coeff;
    return value;
}

// ---------------------------------------------------------------------------
// Sg9Dsp — lifecycle
// ---------------------------------------------------------------------------
Sg9Dsp::Sg9Dsp() = default;

static void setOscBankSr(WavetableOsc* oscs, int n, double sr) {
    for (int i = 0; i < n; ++i) oscs[i].setSampleRate(sr);
}

void Sg9Dsp::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    invSampleRate_ = 1.0 / sampleRate;
    maxBlock_ = 4096;

    // Wavetables.
    reedTable_.build(kReedWave, kNumHarmonics, kWavetableSize);
    majorReedTable_.build(kMajorReedWave, kNumHarmonics, kWavetableSize);
    jawariTable_.build(kJawariWave, kNumHarmonics, kWavetableSize);
    {
        float sineC[3] = { 0.0f, 1.0f, 0.0f };
        sineTable_.build(sineC, 2, 2048);
    }
    {
        // Triangle: 8/pi^2 * sum_{odd k} (-1)^((k-1)/2)/k^2 * sin(kx).
        float triC[17] = {};
        const float s = 8.0f / (float)(kPi * kPi);
        for (int k = 1; k < 17; k += 2)
            triC[k] = s * ((k % 4 == 1) ? 1.0f : -1.0f) / (float)(k * k);
        triTable_.build(triC, 17, 2048);
    }

    // Buses.
    busDroneL_.assign(maxBlock_, 0.0f); busDroneR_.assign(maxBlock_, 0.0f);
    busPadsL_.assign(maxBlock_, 0.0f);  busPadsR_.assign(maxBlock_, 0.0f);
    busPulseL_.assign(maxBlock_, 0.0f); busPulseR_.assign(maxBlock_, 0.0f);
    busDrumsL_.assign(maxBlock_, 0.0f); busDrumsR_.assign(maxBlock_, 0.0f);
    busSpaceSendL_.assign(maxBlock_, 0.0f); busSpaceSendR_.assign(maxBlock_, 0.0f);
    busSpaceDelayL_.assign(maxBlock_, 0.0f); busSpaceDelayR_.assign(maxBlock_, 0.0f);
    busMixL_.assign(maxBlock_, 0.0f);   busMixR_.assign(maxBlock_, 0.0f);
    convInL_.assign(maxBlock_, 0.0f);   convInR_.assign(maxBlock_, 0.0f);
    convOutL_.assign(maxBlock_, 0.0f); convOutR_.assign(maxBlock_, 0.0f);

    // Sample rates: drone oscs.
    setOscBankSr(droneCore_, 2, sampleRate);
    setOscBankSr(droneLeft_, 2, sampleRate);
    setOscBankSr(droneRight_, 2, sampleRate);
    setOscBankSr(droneOctSL_, 2, sampleRate);
    setOscBankSr(droneOctSR_, 2, sampleRate);
    setOscBankSr(droneOctH2_, 2, sampleRate);
    setOscBankSr(droneOctH3_, 2, sampleRate);
    setOscBankSr(droneJawari_, 3, sampleRate);
    setOscBankSr(droneMinorLo_, 2, sampleRate);
    setOscBankSr(droneMinorHi_, 2, sampleRate);
    setOscBankSr(droneMajorLo_, 2, sampleRate);
    setOscBankSr(droneMajorHi_, 2, sampleRate);
    droneCore_[0].setTable(&reedTable_); droneCore_[1].setTable(&reedTable_);
    droneLeft_[0].setTable(&reedTable_); droneLeft_[1].setTable(&reedTable_);
    droneRight_[0].setTable(&reedTable_); droneRight_[1].setTable(&reedTable_);
    droneOctSL_[0].setTable(&reedTable_); droneOctSL_[1].setTable(&reedTable_);
    droneOctSR_[0].setTable(&reedTable_); droneOctSR_[1].setTable(&reedTable_);
    droneOctH2_[0].setTable(&reedTable_); droneOctH2_[1].setTable(&reedTable_);
    droneOctH3_[0].setTable(&reedTable_); droneOctH3_[1].setTable(&reedTable_);
    droneJawari_[0].setTable(&jawariTable_); droneJawari_[1].setTable(&jawariTable_);
    droneJawari_[2].setTable(&jawariTable_);
    droneMinorLo_[0].setTable(&reedTable_);
    droneMinorHi_[0].setTable(&reedTable_);
    droneMajorLo_[0].setTable(&majorReedTable_);
    droneMajorHi_[0].setTable(&majorReedTable_);

    // Drone filters.
    droneBodyLpL_.setSampleRate(sampleRate); droneBodyLpR_.setSampleRate(sampleRate);
    droneJawHpL_.setSampleRate(sampleRate); droneJawHpR_.setSampleRate(sampleRate);
    droneJawLpL_.setSampleRate(sampleRate); droneJawLpR_.setSampleRate(sampleRate);
    droneJawHpL_.setHighpass(kJawariHpFreq, kJawariHpQ);
    droneJawHpR_.setHighpass(kJawariHpFreq, kJawariHpQ);
    droneJawLpL_.setLowpass(kJawariLpFreq, kJawariLpQ);
    droneJawLpR_.setLowpass(kJawariLpFreq, kJawariLpQ);
    for (int k = 0; k < 3; ++k) {
        droneApL_[k].setSampleRate(sampleRate);
        droneApR_[k].setSampleRate(sampleRate);
        droneApL_[k].setAllpass(kApFreq[k], kApQ[k]);
        droneApR_[k].setAllpass(kApFreq[k], kApQ[k]);
    }
    droneDelay_.prepare(sampleRate, 1.0f);
    droneDelay_.setDelaySeconds(kDroneDelaySec);
    droneDelayLp_.setSampleRate(sampleRate);
    droneDelayLp_.setLowpass(kDroneDelayLpHz, 1.0f); // Web Audio default Q

    minorFollow_.setSampleRate(sampleRate);
    minorFollow_.setTimeConstant(kHarmonySmoothSec);
    majorFollow_.setSampleRate(sampleRate);
    majorFollow_.setTimeConstant(kHarmonySmoothSec);

    // Pad/beat/space blocks (owned by later chunks; prepared here so the
    // object is fully usable after prepare()).
    revPreOsc_.setSampleRate(sampleRate); revPreOsc_.setTable(&triTable_);
    revPreLp_.setSampleRate(sampleRate);
    for (int i = 0; i < kMaxPadNotes; ++i) {
        padNotes_[i].osc.setSampleRate(sampleRate);
        padNotes_[i].tri.setSampleRate(sampleRate);
        padNotes_[i].osc.setTable(&sineTable_);
        padNotes_[i].tri.setTable(&triTable_);
        padNotes_[i].lp.setSampleRate(sampleRate);
    }
    steelDelay_.prepare(sampleRate, 5.0f);
    for (int i = 0; i < kSteelTaps; ++i) steelTapLp_[i].setSampleRate(sampleRate);
    steelFbLp_.setSampleRate(sampleRate);
    steelFbLp_.setLowpass(kSteelFbLpHz, 0.22f);
    revDelay_.prepare(sampleRate, 4.0f);
    revFbLp_.setSampleRate(sampleRate);
    revFbLp_.setLowpass(kRevFbLpHz, 0.25f);

    tomBody_.setSampleRate(sampleRate); tomBody_.setTable(&sineTable_);
    tomSub_.setSampleRate(sampleRate);  tomSub_.setTable(&sineTable_);
    tomLp_.setSampleRate(sampleRate);
    sirenOsc_.setSampleRate(sampleRate); sirenOsc_.setTable(&sineTable_);
    sirenLp_.setSampleRate(sampleRate);
    sirenDly_.prepare(sampleRate, 1.5f);
    sirenDly_.setDelaySeconds(kSirenEchoSec);
    sirenEchoLp_.setSampleRate(sampleRate);
    sirenEchoLp_.setLowpass(220.0f, 0.18f);
    for (int i = 0; i < kMaxBeatHits; ++i) {
        beatHits_[i].oscA.setSampleRate(sampleRate);
        beatHits_[i].oscB.setSampleRate(sampleRate);
        beatHits_[i].oscA.setTable(&sineTable_);
        beatHits_[i].oscB.setTable(&sineTable_);
        beatHits_[i].filt.setSampleRate(sampleRate);
    }
    for (int i = 0; i < kWoodTaps; ++i) {
        woodOsc_[i].setSampleRate(sampleRate);
        woodOsc_[i].setTable(&sineTable_);
        woodLp_[i].setSampleRate(sampleRate);
    }
    drumToneLp_.setSampleRate(sampleRate);
    drumToneLp_.setLowpass(kDrumLpHz, 0.18f);

    convolver_.prepare(sampleRate, kSpaceIrSeconds, kConvBlockSize);
    for (int i = 0; i < 4; ++i) {
        spaceEqL_[i].setSampleRate(sampleRate);
        spaceEqR_[i].setSampleRate(sampleRate);
    }
    // Space return EQ (web): HP 250 -> -9 dB @720 -> -5.5 dB @1550 ->
    // +1.5 dB shelf @4800.
    for (int ch = 0; ch < 2; ++ch) {
        Biquad* eq = (ch == 0) ? spaceEqL_ : spaceEqR_;
        eq[0].setHighpass(kSpaceEqHpFreq, kSpaceEqHpQ);
        eq[1].setPeaking(kSpaceEqPk1Freq, kSpaceEqPk1Q, kSpaceEqPk1Db);
        eq[2].setPeaking(kSpaceEqPk2Freq, kSpaceEqPk2Q, kSpaceEqPk2Db);
        eq[3].setHighshelf(kSpaceEqShelfFreq, kSpaceEqShelfDb);
    }
    decorrL_.prepare(sampleRate, 0.2f); decorrR_.prepare(sampleRate, 0.2f);
    decorrL_.setDelaySeconds(kSpaceDecorrL);
    decorrR_.setDelaySeconds(kSpaceDecorrR);
    // Stereo drone delay (second channel added by chunk D).
    droneDelayR_.prepare(sampleRate, 1.0f);
    droneDelayR_.setDelaySeconds(kDroneDelaySec);
    droneDelayLpR_.setSampleRate(sampleRate);
    droneDelayLpR_.setLowpass(kDroneDelayLpHz, 1.0f);
    drumToneLpR_.setSampleRate(sampleRate);
    drumToneLpR_.setLowpass(kDrumLpHz, 0.18f);
    // Felt bass: 40 Hz sine -> LP 88 -> .024 (always on).
    feltOsc_.setSampleRate(sampleRate);
    feltOsc_.setTable(&sineTable_);
    feltOsc_.setFreq(kFeltFreq);
    feltLp_.setSampleRate(sampleRate);
    feltLp_.setLowpass(kFeltLpHz, 0.35f);

    buildSpaceIR();       // 30 s stereo noise IR x (1-t)^.68 -> convolver

    buildPadCurves(); // chunk C fills the curve tables (safe to call now)
    reset();
}

void Sg9Dsp::reset() {
    rng_.seed(0x51ab3d21u);
    activeVoice_ = -1;
    prevVoice_ = -1;
    voiceEnv_ = 0.0f;
    voicePeak_ = 0.0f;
    prevVoiceEnv_ = 0.0f;
    attackT_ = 0.0f; relT_ = 0.0f; relStartEnv_ = 0.0f;
    attacking_ = false; releasing_ = false;
    outMeterL_ = outMeterR_ = 0.0f;
    for (int i = 0; i < kNumChakras; ++i) voiceLevelSm_[i] = 0.0f;

    WavetableOsc* allDrone[] = {
        &droneCore_[0], &droneCore_[1], &droneLeft_[0], &droneLeft_[1],
        &droneRight_[0], &droneRight_[1], &droneOctSL_[0], &droneOctSL_[1],
        &droneOctSR_[0], &droneOctSR_[1], &droneOctH2_[0], &droneOctH2_[1],
        &droneOctH3_[0], &droneOctH3_[1], &droneJawari_[0], &droneJawari_[1],
        &droneJawari_[2], &droneMinorLo_[0], &droneMinorHi_[0],
        &droneMajorLo_[0], &droneMajorHi_[0],
    };
    for (auto* o : allDrone) o->reset(0.0f);

    droneBodyLpL_.reset(); droneBodyLpR_.reset();
    droneJawHpL_.reset(); droneJawHpR_.reset();
    droneJawLpL_.reset(); droneJawLpR_.reset();
    for (int k = 0; k < 3; ++k) { droneApL_[k].reset(); droneApR_[k].reset(); }
    droneDelay_.reset(); droneDelayLp_.reset();
    minorFollow_.reset(params_.padVoicing == kPadMinorThird ? 1.0f : 0.0f);
    majorFollow_.reset(params_.padVoicing == kPadMajorThird ? 1.0f : 0.0f);

    driftClock_ = 0.0f; driftNextRetrig_ = kDriftStartSec; driftLastRetrig_ = 0.0f;
    driftFilter_ = driftFilterT_ = driftFilterS_ = 1.0f;
    driftCore_ = driftCoreT_ = driftCoreS_ = 1.0f;
    driftDetune_ = driftDetuneT_ = driftDetuneS_ = 1.9f;
    driftJawari_ = driftJawariT_ = driftJawariS_ = 1.0f;
    driftH3_ = driftH3T_ = driftH3S_ = 1.0f;
    for (int k = 0; k < 3; ++k) apLfoPhase_[k] = 0.0f;
    orbitPhase_ = 0.0f;
    droneCutoff_ = droneBaseCut_ = 2000.0f;
    dronePreGain_ = kDronePreGain;
    droneBusRms_ = 0.0f;
    droneCutTarget_ = 2000.0f;

    // Later-chunk state (safe defaults).
    for (int i = 0; i < kMaxPadNotes; ++i) padNotes_[i].active = false;
    padStep_ = 0; padStepClock_ = 0.0f; breathPhase_ = 0.0f;
    revPreActive_ = false;
    steelDelay_.reset(); revDelay_.reset();
    tomActive_ = false; sirenActive_ = false;
    sirenDly_.reset();
    for (int i = 0; i < kMaxBeatHits; ++i) beatHits_[i].active = false;
    for (int i = 0; i < kWoodTaps; ++i) {
        woodTaps_[i].active = false;
        woodTaps_[i].sounding = false;
    }
    beatOrbitPhase_ = 0.0f; drumCompEnv_ = 0.0f;
    drumToneLpR_.reset();
    droneDelayR_.reset(); droneDelayLpR_.reset();
    feltOsc_.reset(0.0f); feltLp_.reset();
    schedClock_ = 0.0f; nextBeatAt_ = 0.0f;
    beatInPattern_ = 0; patternCycle_ = 0; pulsePhrase_ = 0; dayanAlt_ = false;
    convolver_.reset();
    decorrL_.reset(); decorrR_.reset();
    bloomL_ = bloomR_ = 0.0f;
    float bpm = getBpm();
    beatSec_ = 60.0f / bpm;
    eighthSec_ = beatSec_ * 0.5f;
}

void Sg9Dsp::setParams(const Sg9Params& p) {
    const bool droneToneChanged = (p.droneTone != params_.droneTone);
    const bool rhythmChanged = (p.beatPattern != params_.beatPattern) ||
                               (p.tempoSource != params_.tempoSource) ||
                               (p.beatSpeed != params_.beatSpeed);
    params_ = p;
    if (params_.droneTone < 0) params_.droneTone = 0;
    if (params_.droneTone >= kNumDroneTones) params_.droneTone = kNumDroneTones - 1;
    if (params_.padVoicing < 0) params_.padVoicing = 0;
    if (params_.padVoicing >= kNumPadVoicings) params_.padVoicing = kNumPadVoicings - 1;
    if (params_.beatPattern < 0) params_.beatPattern = 0;
    if (params_.beatPattern >= kNumBeatPatterns) params_.beatPattern = kNumBeatPatterns - 1;
    minorFollow_.setTarget(params_.padVoicing == kPadMinorThird ? 1.0f : 0.0f);
    majorFollow_.setTarget(params_.padVoicing == kPadMajorThird ? 1.0f : 0.0f);
    if (droneToneChanged && activeVoice_ >= 0)
        configureDroneForVoice(activeVoice_, false); // live retune, keep phases
    const float bpm = getBpm();
    beatSec_ = 60.0f / bpm;
    eighthSec_ = beatSec_ * 0.5f;
    // Musical scheduler restart on tempo/pattern/speed change (web
    // restartDrumClock): pattern restarts from beat 0 shortly; the pulse
    // grace counters are NOT reset.
    if (rhythmChanged && activeVoice_ >= 0) {
        beatInPattern_ = 0;
        dayanAlt_ = false;
        nextBeatAt_ = schedClock_ + 0.08f;
    }
}

// --- small helpers ---
float Sg9Dsp::midiToFreq(int midi) {
    return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
}
int Sg9Dsp::nearestChakra(float freq) const {
    int best = kDefaultVoice;
    float bestDist = 1e30f;
    for (int i = 0; i < kNumChakras; ++i) {
        const float d = std::fabs(std::log2(freq / kChakras[i].freq));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}
float Sg9Dsp::getBpm() const {
    int ts = params_.tempoSource;
    if (ts < 0) ts = 0; if (ts > 4) ts = 4;
    int sp = params_.beatSpeed;
    if (sp < 0) sp = 0; if (sp > 2) sp = 2;
    return kTempoSources[ts].bpm * kSpeedScale[sp];
}
float Sg9Dsp::foldFrequency(float hz, float lo, float hi) const {
    int guard = 0;
    while (hz > hi && guard++ < 24) hz *= 0.5f;
    guard = 0;
    while (hz < lo && guard++ < 24) hz *= 2.0f;
    return hz;
}

void Sg9Dsp::triggerVoice(int index) {
    if (index < 0 || index >= kNumChakras) return;
    if (index == activeVoice_ && !releasing_) {
        releaseVoice(index); // tap active voice: stop all sound
        return;
    }
    prevVoice_ = activeVoice_;
    prevVoiceEnv_ = voiceEnv_;
    activeVoice_ = index;
    configureDroneForVoice(index, true);
    const float lvl = (float)params_.voiceLevels[index] / 100.0f;
    voicePeak_ = kVoicePeakGain * std::pow(std::max(lvl, 0.0f), kVoicePeakExp);
    // Continue the exponential attack from the current envelope level so a
    // voice switch doesn't click: solve k for env = .0001*10000^k.
    const float e0 = std::max(voiceEnv_, 0.0001f);
    const float k0 = std::log(e0 / 0.0001f) / std::log(10000.0f);
    attackT_ = std::min(std::max(k0, 0.0f), 1.0f) * kVoiceAttackSec;
    attacking_ = true;
    releasing_ = false;
    // Restart the slow drift clock for the new voice.
    driftClock_ = 0.0f; driftNextRetrig_ = kDriftStartSec; driftLastRetrig_ = 0.0f;
    driftFilter_ = driftFilterT_ = driftFilterS_ = 1.0f;
    driftCore_ = driftCoreT_ = driftCoreS_ = 1.0f;
    driftDetune_ = driftDetuneT_ = driftDetuneS_ = 1.9f;
    driftJawari_ = driftJawariT_ = driftJawariS_ = 1.0f;
    driftH3_ = driftH3T_ = driftH3S_ = 1.0f;
    // Restart the beat scheduler for the new voice (web startVoice:
    // drumNextTime = now + .14). Pulse grace restarts via restartPadPhrase()
    // on the first renderPads() for the new chakra.
    schedClock_ = 0.0f;
    nextBeatAt_ = 0.14f;
    beatInPattern_ = 0;
    dayanAlt_ = false;
}

void Sg9Dsp::releaseVoice(int index) {
    if (index >= 0 && index != activeVoice_) return;
    if (activeVoice_ < 0) return;
    releasing_ = true;
    attacking_ = false;
    relT_ = 0.0f;
    relStartEnv_ = std::max(voiceEnv_, 0.0001f);
}

void Sg9Dsp::noteOn(int midiNote, float /*velocity*/) {
    triggerVoice(nearestChakra(midiToFreq(midiNote)));
}
void Sg9Dsp::noteOff(int /*midiNote*/) {
    releaseVoice(-1);
}

int Sg9Dsp::getActiveTrigger() const { return activeVoice_; }
int Sg9Dsp::getLatencySamples() const { return convolver_.latencySamples(); }
float Sg9Dsp::getOutMeterL() const { return outMeterL_; }
float Sg9Dsp::getOutMeterR() const { return outMeterR_; }
float Sg9Dsp::getVoiceLevel(int voice) const {
    if (voice < 0 || voice >= kNumChakras) return 0.0f;
    return voiceLevelSm_[voice];
}

// ---------------------------------------------------------------------------
// Drone voice
// ---------------------------------------------------------------------------
void Sg9Dsp::configureDroneForVoice(int voiceIdx, bool resetPhases) {
    if (voiceIdx < 0 || voiceIdx >= kNumChakras) return;
    const float base = kChakras[voiceIdx].freq;
    const ShrutiMode& mode = kShrutiModes[params_.droneTone];
    droneBaseHz_ = base;

    auto cfgPair = [&](WavetableOsc* pair, float freq, float dA, float dB) {
        for (int k = 0; k < 2; ++k) {
            pair[k].setFreq(freq);
            pair[k].setDetuneCents(k == 0 ? dA : dB);
            if (resetPhases) pair[k].reset(0.0f);
        }
    };
    cfgPair(droneCore_,  base * mode.ratios[0], kReedPairDetuneCore[0],  kReedPairDetuneCore[1]);
    cfgPair(droneLeft_,  base * mode.ratios[1], kReedPairDetuneLeft[0],  kReedPairDetuneLeft[1]);
    cfgPair(droneRight_, base * mode.ratios[2], kReedPairDetuneRight[0], kReedPairDetuneRight[1]);
    cfgPair(droneOctSL_, 2.0f * base * mode.ratios[0], kOctDetuneSL[0], kOctDetuneSL[1]);
    cfgPair(droneOctSR_, 2.0f * base * mode.ratios[1], kOctDetuneSR[0], kOctDetuneSR[1]);
    cfgPair(droneOctH2_, 2.0f * base * mode.ratios[2], kOctDetuneH2[0], kOctDetuneH2[1]);
    cfgPair(droneOctH3_, 3.0f * base,                  kOctDetuneH3[0], kOctDetuneH3[1]);
    for (int j = 0; j < 3; ++j) {
        droneJawari_[j].setFreq(base * mode.ratios[j]);
        droneJawari_[j].setDetuneCents(kJawariDetune[j]);
        if (resetPhases) droneJawari_[j].reset(0.0f);
    }
    droneMinorLo_[0].setFreq(base * kMinorLoRatio);
    droneMinorHi_[0].setFreq(base * kMinorHiRatio);
    droneMajorLo_[0].setFreq(base * kMajorLoRatio);
    droneMajorHi_[0].setFreq(base * kMajorHiRatio);
    if (resetPhases) {
        droneMinorLo_[0].reset(0.0f); droneMinorHi_[0].reset(0.0f);
        droneMajorLo_[0].reset(0.0f); droneMajorHi_[0].reset(0.0f);
    }

    // Body filter: baseCut = clamp(base*3.2, 1900, 3600); start at *.76.
    droneBaseCut_ = std::min(kBodyCutMax, std::max(kBodyCutMin, base * 3.2f));
    droneCutTarget_ = droneBaseCut_;
    droneCutoff_ = droneBaseCut_ * kBodyCutStartScale;
    droneBodyLpL_.setLowpass(droneCutoff_, kBodyFilterQ);
    droneBodyLpR_.setLowpass(droneCutoff_, kBodyFilterQ);
}

void Sg9Dsp::updateDroneDrift(float dt) {
    if (activeVoice_ < 0) return;
    driftClock_ += dt;
    if (driftClock_ >= kDriftStartSec && driftClock_ >= driftNextRetrig_) {
        // Capture ramp starts; pick new targets within +/-depth, clamped.
        driftFilterS_ = driftFilter_;
        driftCoreS_ = driftCore_;
        driftDetuneS_ = driftDetune_;
        driftJawariS_ = driftJawari_;
        driftH3S_ = driftH3_;
        driftFilterT_ = std::min(1.07f, std::max(0.91f, 1.0f + rng_.nextRange(-kDriftFilterDepth, kDriftFilterDepth)));
        driftCoreT_   = std::min(1.06f, std::max(0.91f, 1.0f + rng_.nextRange(-kDriftCoreDepth, kDriftCoreDepth)));
        driftDetuneT_ = std::min(kDriftDetuneMaxCents, std::max(kDriftDetuneMinCents,
                            1.9f * (1.0f + rng_.nextRange(-kDriftDetuneDepth, kDriftDetuneDepth))));
        driftJawariT_ = std::min(0.084f / kJawariBaseGain, std::max(0.056f / kJawariBaseGain,
                            1.0f + rng_.nextRange(-kDriftJawariDepth, kDriftJawariDepth)));
        driftH3T_     = std::min(0.0092f / kH3BaseGain, std::max(0.0058f / kH3BaseGain,
                            1.0f + rng_.nextRange(-kDriftH3Depth, kDriftH3Depth)));
        driftLastRetrig_ = driftClock_;
        driftNextRetrig_ = driftClock_ + rng_.nextRange(kDriftRetrigMinSec, kDriftRetrigMaxSec);
    }
    const float span = driftNextRetrig_ - driftLastRetrig_;
    const float t01 = span > 0.0f
        ? std::min(1.0f, std::max(0.0f, (driftClock_ - driftLastRetrig_) / span))
        : 1.0f;
    driftFilter_ = driftFilterS_ + (driftFilterT_ - driftFilterS_) * t01;
    driftCore_   = driftCoreS_   + (driftCoreT_   - driftCoreS_)   * t01;
    driftDetune_ = driftDetuneS_ + (driftDetuneT_ - driftDetuneS_) * t01;
    driftJawari_ = driftJawariS_ + (driftJawariT_ - driftJawariS_) * t01;
    driftH3_     = driftH3S_     + (driftH3T_     - driftH3S_)     * t01;
    // Apply: filter target, o1b detune (core pair, osc index 1).
    droneCutTarget_ = droneBaseCut_ * driftFilter_;
    droneCore_[1].setDetuneCents(driftDetune_);
}

void Sg9Dsp::updateVoiceEnvelope(int /*n*/) {
    // Envelope is evaluated per-sample inside renderDrone (this keeps the
    // exponential attack/release click-free); this hook exists for the
    // header-declared API and future per-block work.
}

// Equal-power stereo pan add (matches Web Audio StereoPannerNode law).
inline void panAdd(float s, float pan, float& l, float& r) {
    const float a = (pan + 1.0f) * (float)(kPi / 4.0);
    l += s * std::cos(a);
    r += s * std::sin(a);
}

void Sg9Dsp::renderDroneReeds(float* busL, float* busR, int n) {
    const ShrutiMode& mode = kShrutiModes[params_.droneTone];
    const float gCore = mode.levels[0] * driftCore_;
    const float gL = mode.levels[1];
    const float gR = mode.levels[2];
    const float gSL = mode.levels[0] * kOctGainScaleA;
    const float gSR = mode.levels[1] * kOctGainScaleA;
    const float gH2 = mode.levels[2] * kOctGainScaleB;
    const float gH3 = kH3BaseGain * driftH3_;
    for (int i = 0; i < n; ++i) {
        float l = 0.0f, r = 0.0f, s;
        // Core / left / right reed pairs (.62/.38 pair balance).
        s = (droneCore_[0].tick() * kReedPairGainA + droneCore_[1].tick() * kReedPairGainB) * gCore;
        panAdd(s, kReedPanCore, l, r);
        s = (droneLeft_[0].tick() * kReedPairGainA + droneLeft_[1].tick() * kReedPairGainB) * gL;
        panAdd(s, kReedPanLeft, l, r);
        s = (droneRight_[0].tick() * kReedPairGainA + droneRight_[1].tick() * kReedPairGainB) * gR;
        panAdd(s, kReedPanRight, l, r);
        // Octave doublings.
        s = (droneOctSL_[0].tick() * kReedPairGainA + droneOctSL_[1].tick() * kReedPairGainB) * gSL;
        panAdd(s, kOctPanSL, l, r);
        s = (droneOctSR_[0].tick() * kReedPairGainA + droneOctSR_[1].tick() * kReedPairGainB) * gSR;
        panAdd(s, kOctPanSR, l, r);
        s = (droneOctH2_[0].tick() * kReedPairGainA + droneOctH2_[1].tick() * kReedPairGainB) * gH2;
        panAdd(s, kOctPanH2, l, r);
        s = (droneOctH3_[0].tick() * kReedPairGainA + droneOctH3_[1].tick() * kReedPairGainB) * gH3;
        panAdd(s, kOctPanH3, l, r);
        busL[i] += l;
        busR[i] += r;
    }
}

void Sg9Dsp::renderDroneJawari(float* busL, float* busR, int n) {
    const ShrutiMode& mode = kShrutiModes[params_.droneTone];
    const float g = kJawariBaseGain * driftJawari_;
    for (int i = 0; i < n; ++i) {
        float j = droneJawari_[0].tick() * mode.levels[0]
                + droneJawari_[1].tick() * mode.levels[1]
                + droneJawari_[2].tick() * mode.levels[2];
        // Mono jawari thread: HP 520 -> LP 3100 -> gain -> both channels.
        j = droneJawHpL_.process(j);
        j = droneJawLpL_.process(j);
        j *= g;
        busL[i] += j;
        busR[i] += j;
    }
}

void Sg9Dsp::renderDroneColorReeds(float* busL, float* busR, int n) {
    for (int i = 0; i < n; ++i) {
        const float minorF = minorFollow_.tick();
        const float majorF = majorFollow_.tick();
        float l = 0.0f, r = 0.0f, s;
        if (minorF > 0.0001f) {
            s = droneMinorLo_[0].tick() * (kMinorGainA * minorF);
            panAdd(s, -kMinorPan, l, r);
            s = droneMinorHi_[0].tick() * (kMinorGainB * minorF);
            panAdd(s, kMinorPan, l, r);
        } else {
            droneMinorLo_[0].tick(); droneMinorHi_[0].tick(); // keep phase running
        }
        if (majorF > 0.0001f) {
            s = droneMajorLo_[0].tick() * (kMajorGainA * majorF);
            panAdd(s, -kMajorPan, l, r);
            s = droneMajorHi_[0].tick() * (kMajorGainB * majorF);
            panAdd(s, kMajorPan, l, r);
        } else {
            droneMajorLo_[0].tick(); droneMajorHi_[0].tick();
        }
        busL[i] += l;
        busR[i] += r;
    }
}

void Sg9Dsp::renderDrone(float* busL, float* busR, int n) {
    if (activeVoice_ < 0 && !releasing_) return;
    const float dt = (float)invSampleRate_;

    updateDroneDrift((float)n * dt);

    // Body cutoff eases toward target (tau ~3.4 s); update coeffs per block.
    {
        const float c = 1.0f - std::exp(-(float)n * dt / kBodyCutTauSec);
        const float newCut = droneCutoff_ + (droneCutTarget_ - droneCutoff_) * c;
        if (std::fabs(newCut - droneCutoff_) > 0.05f) {
            droneCutoff_ = newCut;
            droneBodyLpL_.setLowpass(droneCutoff_, kBodyFilterQ);
            droneBodyLpR_.setLowpass(droneCutoff_, kBodyFilterQ);
        }
    }
    // All-pass LFOs: advance phase per block, retune all-pass freqs.
    for (int k = 0; k < 3; ++k) {
        apLfoPhase_[k] += (float)(2.0 * kPi * kApLfoHz[k] * n * dt);
        if (apLfoPhase_[k] > 2.0f * (float)kPi) apLfoPhase_[k] -= 2.0f * (float)kPi;
        const float f = kApFreq[k] + kApLfoDepthHz[k] * std::sin(apLfoPhase_[k]);
        droneApL_[k].setAllpass(std::max(40.0f, f), kApQ[k]);
        droneApR_[k].setAllpass(std::max(40.0f, f), kApQ[k]);
    }
    // Orbit panner LFO.
    orbitPhase_ += (float)(2.0 * kPi * kOrbitLfoHz * n * dt);
    if (orbitPhase_ > 2.0f * (float)kPi) orbitPhase_ -= 2.0f * (float)kPi;
    const float orbitPan = kOrbitLfoDepth * std::sin(orbitPhase_);
    const float oa = (orbitPan + 1.0f) * (float)(kPi / 4.0);
    const float ogL = std::cos(oa), ogR = std::sin(oa);

    // Raw reed sum (mono-ish stereo via per-reed pans).
    renderDroneReeds(busL, busR, n);
    renderDroneJawari(busL, busR, n);
    renderDroneColorReeds(busL, busR, n);

    // Body filter -> dry (.90) + all-pass chain (send .10, return .66) ->
    // orbit pan -> envelope * peak * 1.4x pre-fader gain. In place.
    for (int i = 0; i < n; ++i) {
        float l = droneBodyLpL_.process(busL[i]);
        float r = droneBodyLpR_.process(busR[i]);
        float apl = droneApL_[0].process(droneApL_[1].process(droneApL_[2].process(l * kApSend)));
        float apr = droneApR_[0].process(droneApR_[1].process(droneApR_[2].process(r * kApSend)));
        l = l * kApDry + apl * kApReturn;
        r = r * kApDry + apr * kApReturn;
        const float ol = l * ogL, or_ = r * ogR;

        // Voice envelope: exponential attack (3.2 s) / release (1.5 s).
        float env;
        if (releasing_) {
            relT_ += dt;
            const float k = std::min(relT_ / kVoiceReleaseSec, 1.0f);
            env = relStartEnv_ * std::pow(0.0001f / std::max(relStartEnv_, 0.0001f), k);
            if (k >= 1.0f) {
                releasing_ = false;
                activeVoice_ = -1;
                env = 0.0f;
            }
        } else if (attacking_) {
            attackT_ += dt;
            const float k = std::min(attackT_ / kVoiceAttackSec, 1.0f);
            env = std::pow(10000.0f, k) * 0.0001f;
            if (k >= 1.0f) { attacking_ = false; env = 1.0f; }
        } else {
            env = 1.0f;
        }
        voiceEnv_ = env;
        const float g = env * voicePeak_ * dronePreGain_;
        busL[i] = ol * g;
        busR[i] = or_ * g;
    }

    // Smoothed voice levels for the editor glow.
    for (int v = 0; v < kNumChakras; ++v) {
        const float tgt = (v == activeVoice_) ? voiceEnv_ : 0.0f;
        voiceLevelSm_[v] += (tgt - voiceLevelSm_[v]) * 0.02f;
    }
    { // Test seam: post-envelope drone bus RMS (pre-fader, no felt bass).
        double acc = 0.0;
        for (int di = 0; di < n; ++di)
            acc += 0.5 * ((double)busL[di] * busL[di] + (double)busR[di] * busR[di]);
        droneBusRms_ = (float)std::sqrt(acc / std::max(1, n));
    }
}

} // namespace sg9

namespace sg9 {
// ---------------------------------------------------------------------------
// Chunk C: pads (Deep Blue steel phrase + delays) and pulse (pitch-dive toms
// + dub siren). All render* methods ACCUMULATE into the passed buses; chunk D
// must clearBuses(n) at the top of each process() block.
//
// Call-order contract for chunk D's process():
//   renderPads(busPadsL_, busPadsR_, n)   // voice sum, .22/.92 path weights
//   busPads *= faderPads                   // web feeds delays POST-fader
//   renderPadDelays(busPadsL_, busPadsR_, n) // reads post-fader pads;
//                                          // steel wet -> busSpaceDelay,
//                                          // reverse wet -> busSpaceDelay
//                                          //   (x.42) + busSpaceSend (x.72)
//   renderPulseBeat(busPulseL_, busPulseR_, busDrumsL_, busDrumsR_, n)
//   busPulse *= faderPulse
// renderSpace (chunk D) scales busSpaceDelay by spaceFader*.58 (reverb) /
//   *.50 (delay) and convolves busSpaceSend.
// ---------------------------------------------------------------------------

namespace {
// File-local helpers for chunk C.
float smoothstep01(float x) {
    x = std::min(std::max(x, 0.0f), 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
// Shared 10.9 s breath cycle: 4.4 s inhale smoothstep up, 6.5 s exhale
// smoothstep down, no plateau (web makeBreathSource).
float breath01(float tSec) {
    float c = std::fmod(tSec, kBreathCycleSec);
    if (c < 0.0f) c += kBreathCycleSec;
    if (c < kBreathInhaleSec) return smoothstep01(c / kBreathInhaleSec);
    return 1.0f - smoothstep01((c - kBreathInhaleSec) / kBreathExhaleSec);
}
float curveLerp(const float* curve, int pts, float pos) {
    if (pos <= 0.0f) return curve[0];
    const float maxPos = (float)(pts - 1);
    if (pos >= maxPos) return curve[pts - 1];
    const int i0 = (int)pos;
    const float f = pos - (float)i0;
    return curve[i0] + (curve[i0 + 1] - curve[i0]) * f;
}
// Deterministic pseudo-noise in [-1,1] (stateless; keeps renders identical).
float hashNoise(float t) {
    const float s = std::sin(t * 12743.13f) * 43758.5453f;
    return (s - std::floor(s)) * 2.0f - 1.0f;
}
// Pad note lowpass trajectory: start -> mid over 2.8 s, then settle.
void padCutoffs(float target, float& cs, float& cm, float& cset) {
    cs   = std::min(2700.0f, std::max(1350.0f, target * 4.2f));
    cm   = std::min(1120.0f, std::max(500.0f,  target * 1.55f));
    cset = std::min(920.0f,  std::max(420.0f,  target * 1.24f));
}
} // namespace

void Sg9Dsp::buildPadCurves() {
    // 65-pt double smoothstep: s(x)=x^2(3-2x); c=s(s(x)), floored at .0001.
    for (int i = 0; i < kPadSwellPts; ++i) {
        const float x = (float)i / (float)(kPadSwellPts - 1);
        const float s1 = x * x * (3.0f - 2.0f * x);
        const float c = s1 * s1 * (3.0f - 2.0f * s1);
        padSwell_[i] = std::max(c, 0.0001f);
    }
    for (int i = 0; i < kPadReleasePts; ++i)
        padRelease_[i] = kPadReleaseCurve[i];
    // Steel tap tone filters (web: lowpass 1240-idx*80, Q .22).
    for (int k = 0; k < kSteelTaps; ++k)
        steelTapLp_[k].setLowpass(1240.0f - (float)k * 80.0f, 0.22f);
}

// Pads (+ pulse grace state) follow the active chakra and pad voicing.
// Called from renderPads() when either changes; also restarts the pulse
// grace counters (web: fresh voice.pulse = {pulsePhrase:0, pulseCycle:0}).
void Sg9Dsp::restartPadPhrase() {
    for (auto& note : padNotes_) {
        if (note.active && !note.killing_) {
            note.killing_ = true; // web: 35 ms cancel; we use a 50 ms fade
            note.killT_ = 0.0f;
        }
    }
    revPreActive_ = false;
    padStep_ = 0;
    // First pre-echo fires 0.2 s after (re)trigger so the first note blooms
    // at ~2.55 s, matching the web's makePads/retunePads timing.
    padStepClock_ = kPadStepSec - 0.2f;
    if (activeVoice_ >= 0) {
        const PadVoicingSpec& v = kPadVoicings[params_.padVoicing];
        // Web glides the first note from the voicing's LAST ratio.
        padPrevFreq_ = kChakras[activeVoice_].freq * v.ratios[v.count - 1];
    } else {
        padPrevFreq_ = 0.0f;
    }
    pulsePhrase_ = 0;
    patternCycle_ = 0;
    tomActive_ = false;
    sirenActive_ = false;
}

void Sg9Dsp::startPadStep() {
    if (activeVoice_ < 0) return;
    const float base = kChakras[activeVoice_].freq;
    const PadVoicingSpec& v = kPadVoicings[params_.padVoicing];
    const int idx = padStep_ % v.count;
    const int pc = padStep_ % 12;
    const float arrival = (float)kPadArrivalCents[pc];
    const float target = base * v.ratios[idx] *
        std::pow(2.0f, arrival / 1200.0f);
    const float pan = kPadPanPattern[pc];

    // Reverse pre-echo starts NOW (2.35 s before the note): triangle at
    // target*.5, detune sweeps -9 -> arrival cents, LP 420->1240 Hz,
    // envelope peaks at .17, pan = -directPan*.82 (web scheduleSteelNote).
    revPreOsc_.setFreq(target * 0.5f);
    revPreOsc_.setDetuneCents(-9.0f);
    revPreOsc_.reset(0.0f);
    revPreLp_.reset();
    revPreActive_ = true;
    revPreT_ = 0.0f;
    revPrePan_ = pan * kPadRevPanScale;
    revPreArrival_ = arrival;

    // Pending note: occupies a pool slot with negative age; the note blooms
    // kPadRevPreSec later (see renderPadNotes).
    PadNote* slot = nullptr;
    for (auto& nn : padNotes_) {
        if (!nn.active) { slot = &nn; break; }
    }
    if (!slot) slot = &padNotes_[0]; // pool exhausted: steal oldest
    slot->active = true;
    slot->killing_ = false;
    slot->killT_ = 0.0f;
    slot->fromFreq = padPrevFreq_;
    slot->targetFreq = target;
    slot->freq = target;
    slot->glideT = 0.0f;
    slot->ageSec = -kPadRevPreSec;
    slot->pan = pan;
    slot->vibPhase = 0.0f;
    slot->stepIndex = padStep_;
    float cs, cm, cset;
    padCutoffs(target, cs, cm, cset);
    slot->lpCut_ = cs;
    slot->lp.reset();
    slot->osc.reset(0.0f);
    slot->tri.reset(0.0f);
    padPrevFreq_ = target;
    ++padStep_;
}

void Sg9Dsp::renderPads(float* busL, float* busR, int n) {
    const float dt = (float)invSampleRate_;
    // Pads follow the active chakra + pad voicing live (web retunePads).
    if (activeVoice_ != padVoiceIdx_ || params_.padVoicing != padVoicingIdx_) {
        restartPadPhrase();
        padVoiceIdx_ = activeVoice_;
        padVoicingIdx_ = params_.padVoicing;
    }
    breathPhase_ += (float)n * dt; // shared 10.9 s cycle (master uses it too)

    if (activeVoice_ >= 0 && !releasing_) {
        padStepClock_ += (float)n * dt;
        while (padStepClock_ >= kPadStepSec) {
            padStepClock_ -= kPadStepSec;
            startPadStep();
        }
    }

    renderPadNotes(busL, busR, n);
    renderReversePreEcho(busL, busR, n);
}

void Sg9Dsp::renderPadNotes(float* busL, float* busR, int n) {
    const float dt = (float)invSampleRate_;
    const float breath = breath01(breathPhase_);

    // Per-block lowpass trajectory updates (web: 2.8 s sweep, then settle).
    for (auto& note : padNotes_) {
        if (!note.active || note.ageSec < 0.0f) continue;
        float cs, cm, cset;
        padCutoffs(note.targetFreq, cs, cm, cset);
        const float t = note.ageSec;
        if (t < 2.8f) {
            note.lpCut_ = cs * std::pow(cm / cs, t / 2.8f);
        } else {
            const float c = 1.0f - std::exp(-(float)n * dt / 1.4f);
            note.lpCut_ += (cset - note.lpCut_) * c;
        }
        note.lp.setLowpass(note.lpCut_, 0.48f);
    }

    for (int i = 0; i < n; ++i) {
        float dl = 0.0f, dr = 0.0f;
        for (auto& note : padNotes_) {
            if (!note.active) continue;
            float fade = 1.0f;
            if (note.killing_) {
                note.killT_ += dt;
                fade = 1.0f - note.killT_ / 0.05f;
                if (fade <= 0.0f) { note.active = false; continue; }
            }
            note.ageSec += dt;
            const float age = note.ageSec;
            if (age < 0.0f) continue; // pending: pre-echo window
            if (age >= kPadNoteLenSec) { note.active = false; continue; }

            // Envelope: 4 s double-smoothstep swell -> sustain -> 2 s
            // release beginning exactly at the next step (crossfade).
            float env;
            if (age < kPadSwellSec) {
                env = curveLerp(padSwell_, kPadSwellPts,
                                age / kPadSwellSec * (kPadSwellPts - 1));
            } else if (age < kPadStepSec) {
                env = 1.0f;
            } else {
                env = curveLerp(padRelease_, kPadReleasePts,
                                (age - kPadStepSec) / kPadReleaseSec *
                                (kPadReleasePts - 1));
            }

            // .92 s exponential glide from the previous step's target.
            float fr;
            if (note.glideT < 1.0f && note.fromFreq > 0.0f) {
                note.glideT = std::min(1.0f, note.glideT + dt / kPadGlideSec);
                fr = note.fromFreq *
                    std::pow(note.targetFreq / note.fromFreq, note.glideT);
            } else {
                fr = note.targetFreq;
            }
            note.freq = fr;

            // Hand vibrato: 4.9 Hz +/- per-step pattern, 12 cents depth +
            // 9 cents of breath modulation (web: vibratoDepth 12, breath 9).
            note.vibPhase += (float)(2.0 * kPi) *
                (kPadVibHz + kPadVibPattern[note.stepIndex & 15]) * dt;
            const float cents =
                (kPadVibDepthCents + kPadBreathVibCents * breath) *
                std::sin(note.vibPhase);
            const float vibMult = std::pow(2.0f, cents / 1200.0f);
            note.osc.setFreq(fr * vibMult);
            note.tri.setFreq(fr * kPadTriDetune * vibMult);

            // Sine-led body + quiet triangle companion (web scheduleSteelNote).
            float s = note.osc.tick() + note.tri.tick() * kPadTriGain;
            s = note.lp.process(s);
            s *= env * fade * kPadDirect; // direct path .22 (web pad.direct)

            const float a = (note.pan + 1.0f) * (float)(kPi / 4.0);
            dl += s * std::cos(a);
            dr += s * std::sin(a);
        }
        busL[i] += dl;
        busR[i] += dr;
    }
}

void Sg9Dsp::renderReversePreEcho(float* busL, float* busR, int n) {
    if (!revPreActive_) return;
    const float dt = (float)invSampleRate_;
    // Slow filter/detune sweeps: per-block is plenty over 2.35 s.
    {
        const float t = revPreT_;
        const float cut = kPadRevLpStart *
            std::pow(kPadRevLpEnd / kPadRevLpStart,
                     std::min(t / kPadRevPreSec, 1.0f));
        revPreLp_.setLowpass(cut, 1.0f); // web: default Q (unset)
        const float det = -9.0f + (revPreArrival_ + 9.0f) *
            std::min(t / kPadRevPreSec, 1.0f);
        revPreOsc_.setDetuneCents(det);
    }
    const float a = (revPrePan_ + 1.0f) * (float)(kPi / 4.0);
    const float gL = std::cos(a) * kPadReverse; // reverse path .92
    const float gR = std::sin(a) * kPadReverse;
    for (int i = 0; i < n; ++i) {
        revPreT_ += dt;
        const float t = revPreT_;
        if (t >= 2.47f) { revPreActive_ = false; break; } // preEnd = when+.12
        // Swell to .17 just before the note, then vanish (reversed tape).
        const float env = (t < 2.30f)
            ? 0.0001f * std::pow(1700.0f, t / 2.30f)
            : kPadRevEnvPeak *
              std::pow(0.0001f / kPadRevEnvPeak, (t - 2.30f) / 0.17f);
        float s = revPreLp_.process(revPreOsc_.tick()) * env;
        busL[i] += s * gL;
        busR[i] += s * gR;
    }
}

void Sg9Dsp::renderPadDelays(float* busL, float* busR, int n) {
    // Contract (see file header comment): busL/busR carry the POST-FADER
    // pads signal. Steel wet -> busSpaceDelayL/R; reverse wet ->
    // busSpaceDelayL/R (x.42) and busSpaceSendL/R (x.72). Chunk D's
    // renderSpace applies the SPACE fader scaling.
    for (int i = 0; i < n; ++i) {
        const float mono = 0.5f * (busL[i] + busR[i]);

        // Steel: 8 feed-forward taps at .5..4 s (mono-sum input; taps are
        // hard-panned alternately, which dominates the stereo image).
        // Feedback .65 through the 720 Hz LP, tapped at the 4 s point.
        const float fb = steelFbLp_.process(steelDelay_.readAtSeconds(4.0f));
        steelDelay_.write(mono + fb * kSteelFb);
        for (int k = 0; k < kSteelTaps; ++k) {
            float tap = steelTapLp_[k].process(
                steelDelay_.readAtSeconds(kSteelTapSec[k]));
            tap *= kSteelTapLevel[k] * kSteelWet;
            const float a = (kSteelTapPan[k] + 1.0f) * (float)(kPi / 4.0);
            busSpaceDelayL_[i] += tap * std::cos(a);
            busSpaceDelayR_[i] += tap * std::sin(a);
        }

        // Reverse: 4 stages of .72 s, alternating pans, feedback .38
        // through the 860 Hz LP.
        const float rfb =
            revFbLp_.process(revDelay_.readAtSeconds(kRevStageSec * kRevStages));
        revDelay_.write(mono + rfb * kRevFb);
        for (int k = 0; k < kRevStages; ++k) {
            const float tap =
                revDelay_.readAtSeconds(kRevStageSec * (float)(k + 1)) *
                kRevStageLevel[k];
            const float pan = (k % 2 == 0) ? -kRevStagePan : kRevStagePan;
            const float a = (pan + 1.0f) * (float)(kPi / 4.0);
            const float gL = std::cos(a), gR = std::sin(a);
            busSpaceDelayL_[i] += tap * gL * kRevWetSpaceDelay;
            busSpaceDelayR_[i] += tap * gR * kRevWetSpaceDelay;
            busSpaceSendL_[i] += tap * gL * kRevWetSpaceIn;
            busSpaceSendR_[i] += tap * gR * kRevWetSpaceIn;
        }
    }
}

// ---------------------------------------------------------------------------
// Pulse: sparse pitch-dive toms + dub siren.
// ---------------------------------------------------------------------------

// Called by chunk D's beat scheduler on every beat; pulse speaks only on
// pattern downbeats. Grace: 3 full pattern cycles with NO toms; then one
// tom when (cycle-3) is even. Phrases alternate SUB / ROOT; every 4th
// phrase also fires the dub siren (web scheduleTalaBeat).
void Sg9Dsp::onBeat(int /*beatInPattern*/, bool patternStart, float delaySec) {
    if (!patternStart) return;
    const int cycle = patternCycle_;
    if (params_.pulseOn && activeVoice_ >= 0) {
        if (cycle >= kPulseGraceCycles &&
            ((cycle - kPulseGraceCycles) % 2 == 0)) {
            triggerTom((pulsePhrase_ % 2) == 0, delaySec); // phrase 0 = SUB
            if (((pulsePhrase_ + 1) % 4) == 0) triggerDubSiren(delaySec);
            ++pulsePhrase_;
        }
    }
    ++patternCycle_;
}

void Sg9Dsp::triggerTom(bool sub, float delaySec) {
    if (activeVoice_ < 0) return;
    const float target = kChakras[activeVoice_].freq *
        (sub ? kPulseSubRatio : kPulseRootRatio);
    tomTargetFreq_ = target;
    tomStartFreq_ = std::min(1320.0f,
                             std::max(target * 1.32f, target + 34.0f));
    tomDecay_ = 2.55f + (float)(pulsePhrase_ % 3) * 0.35f;
    tomGain_ = sub ? kPulseSubGain : kPulseRootGain;
    tomPan_ = sub ? -kPulseTomPan : kPulseTomPan;
    tomBody_.reset(0.0f);
    tomSub_.reset(0.0f);
    tomLp_.reset();
    tomActive_ = true;
    tomT_ = -delaySec; // sample-accurate start within the block
}

void Sg9Dsp::renderTom(float* busL, float* busR, int n) {
    if (!tomActive_) return;
    const float dt = (float)invSampleRate_;
    const int lvl = (activeVoice_ >= 0) ? params_.voiceLevels[activeVoice_] : 58;
    // Web pulseBus gain: 0.52 + 0.22*(level/100).
    const float busGain = 0.52f + 0.22f * ((float)lvl / 100.0f);
    const float a = (tomPan_ + 1.0f) * (float)(kPi / 4.0);
    const float gL = std::cos(a) * busGain;
    const float gR = std::sin(a) * busGain;
    // Per-block tone sweep (web: LP start->end over 78% of decay, Q .28).
    {
        const float kd = std::min(std::max(tomT_ / (tomDecay_ * 0.78f), 0.0f), 1.0f);
        const float cs =
            std::min(1450.0f, std::max(310.0f, tomStartFreq_ * 1.08f));
        const float ce =
            std::min(980.0f, std::max(180.0f, tomTargetFreq_ * 1.3f));
        tomLp_.setLowpass(cs * std::pow(ce / cs, kd), 0.28f);
    }
    for (int i = 0; i < n; ++i) {
        tomT_ += dt;
        const float t = tomT_;
        if (t < 0.0f) continue; // pre-delay: sample-accurate beat start
        if (t >= tomDecay_) { tomActive_ = false; break; }
        // Gentle pitch dive into the target over 72% of the decay.
        const float k = std::min(t / (tomDecay_ * kPulseTomDiveFrac), 1.0f);
        const float f =
            tomStartFreq_ * std::pow(tomTargetFreq_ / tomStartFreq_, k);
        tomBody_.setFreq(f);
        tomSub_.setFreq(f * 0.5f);
        // Envelope: .065 s attack -> 48% at half decay -> .0001 at end.
        float env;
        if (t < kPulseTomAttackSec) {
            env = 0.0001f +
                (tomGain_ - 0.0001f) * (t / kPulseTomAttackSec);
        } else if (t < tomDecay_ * 0.5f) {
            env = tomGain_ *
                std::pow(0.48f, (t - kPulseTomAttackSec) /
                                   (tomDecay_ * 0.5f - kPulseTomAttackSec));
        } else {
            env = tomGain_ * 0.48f *
                std::pow(0.0001f / (tomGain_ * 0.48f),
                         (t - tomDecay_ * 0.5f) / (tomDecay_ * 0.5f));
        }
        // Muted noise fleck (deterministic hash noise; web: LP 210-720 Hz,
        // peak .0045, gone by .38 s). Routed through the tom LP — the fleck
        // sits ~30 dB under the tom so the difference is inaudible.
        float fleckEnv = 0.0f;
        if (t < 0.055f) {
            fleckEnv = 0.0001f + (kPulseFleckPeak - 0.0001f) * (t / 0.055f);
        } else if (t < 0.38f) {
            fleckEnv = kPulseFleckPeak *
                std::pow(0.0001f / kPulseFleckPeak, (t - 0.055f) / 0.325f);
        }
        float s = tomBody_.tick() + tomSub_.tick() * 0.055f +
            hashNoise(t * 7.0f) * fleckEnv;
        s = tomLp_.process(s) * env;
        busL[i] += s * gL;
        busR[i] += s * gR;
    }
}

void Sg9Dsp::triggerDubSiren(float delaySec) {
    if (activeVoice_ < 0) return;
    sirenTarget_ = kChakras[activeVoice_].freq * kSirenRatio; // f/4
    sirenLp_.setLowpass(
        std::min(280.0f, std::max(90.0f, sirenTarget_ * 1.18f)), 0.22f);
    sirenEchoLp_.setLowpass(
        std::min(220.0f, std::max(72.0f, sirenTarget_ * 1.04f)), 0.18f);
    sirenOsc_.reset(0.0f);
    sirenActive_ = true;
    sirenT_ = -0.18f - delaySec; // web: 180 ms after the tom + beat offset
}

void Sg9Dsp::renderDubSiren(float* busL, float* busR, int n) {
    if (!sirenActive_) return;
    const float dt = (float)invSampleRate_;
    const float dur = kSirenDurSec;
    const int lvl = (activeVoice_ >= 0) ? params_.voiceLevels[activeVoice_] : 58;
    const float busGain = 0.52f + 0.22f * ((float)lvl / 100.0f);
    for (int i = 0; i < n; ++i) {
        sirenT_ += dt;
        const float t = sirenT_;
        if (t < 0.0f) continue; // start-delay window
        if (t >= dur) { sirenActive_ = false; break; }
        // 1.06x -> 1x over 58% of the duration, then hold.
        const float k = std::min(t / (dur * 0.58f), 1.0f);
        sirenOsc_.setFreq(sirenTarget_ * kSirenStartRatio *
                          std::pow(1.0f / kSirenStartRatio, k));
        // Envelope: 1.1 s swell -> 45% at 64% -> out at 8.4 s.
        float env;
        if (t < 1.1f) {
            env = 0.0001f + (kSirenGain - 0.0001f) * (t / 1.1f);
        } else if (t < dur * 0.64f) {
            env = kSirenGain *
                std::pow(0.45f, (t - 1.1f) / (dur * 0.64f - 1.1f));
        } else {
            env = kSirenGain * 0.45f *
                std::pow(0.0001f / (kSirenGain * 0.45f),
                         (t - dur * 0.64f) / (dur * 0.36f));
        }
        const float s = sirenLp_.process(sirenOsc_.tick()) * env;
        // Slow pan drift -.12 -> +.12.
        const float pan = -0.12f + 0.24f * (t / dur);
        const float a = (pan + 1.0f) * (float)(kPi / 4.0);
        // Dark echo: .72 s delay, feedback .31 through the echo LP, wet .13.
        // (Web pans before the delay; the +/-.12 sweep is negligible, so the
        // mono send is panned with the current pan here.)
        const float dFilt = sirenEchoLp_.process(sirenDly_.read());
        sirenDly_.write(s + dFilt * kSirenEchoFb);
        const float wet = dFilt * kSirenEchoWet + s * 0.018f; // dry .018
        busL[i] += wet * std::cos(a) * busGain;
        busR[i] += wet * std::sin(a) * busGain;
    }
}

void Sg9Dsp::renderPulseBeat(float* pulseL, float* pulseR,
                             float* /*drumL*/, float* /*drumR*/, int n) {
    renderTom(pulseL, pulseR, n);
    renderDubSiren(pulseL, pulseR, n);
    // Drum hits are rendered by chunk D's renderBeatHits into drumL/drumR.
}

// ---------------------------------------------------------------------------
// Chunk D: non-uniform (Gardner) partitioned convolver.
// The 30 s IR runs at half the host rate (24 kHz) — sonically transparent
// for a reverb tail, halves memory and CPU. Partitions start at 128
// conv-rate samples and double in pairs; tail partitions are exact-sized.
// Each partition's input block is FFT'd once per size/baseBlock_ ticks and
// overlap-added into a shared circular output buffer at its IR offset.
// ---------------------------------------------------------------------------
void PartitionedConvolver::fft(float* re, float* im, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / len * (inverse ? 1.0 : -1.0);
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cwr = 1.0, cwi = 0.0;
            for (int j = 0; j < len / 2; ++j) {
                const float ur = re[i + j], ui = im[i + j];
                const float vr = (float)(re[i + j + len / 2] * cwr - im[i + j + len / 2] * cwi);
                const float vi = (float)(re[i + j + len / 2] * cwi + im[i + j + len / 2] * cwr);
                re[i + j] = ur + vr; im[i + j] = ui + vi;
                re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
                const double nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
    if (inverse) {
        const float inv = 1.0f / (float)n;
        for (int i = 0; i < n; ++i) { re[i] *= inv; im[i] *= inv; }
    }
}

void PartitionedConvolver::buildDeciTaps() {
    deciTaps_.assign(64, 0.0f);
    const double fc = 10500.0 / sampleRate_; // cutoff relative to host rate
    for (int k = 0; k < 64; ++k) {
        const double x = (double)k - 31.5;
        const double s = (std::fabs(x) < 1e-9) ? 2.0 * fc
            : std::sin(2.0 * kPi * fc * x) / (kPi * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * k / 63.0)
                       + 0.08 * std::cos(4.0 * kPi * k / 63.0);
        deciTaps_[(size_t)k] = (float)(s * w);
    }
    double sum = 0.0;
    for (float v : deciTaps_) sum += v;
    if (std::fabs(sum) > 1e-9) for (float& v : deciTaps_) v = (float)(v / sum);
}

void PartitionedConvolver::prepare(double sampleRate, float irSeconds, int blockSize) {
    (void)blockSize;
    sampleRate_ = sampleRate;
    convRate_ = sampleRate * 0.5;
    baseBlock_ = 128; // conv-rate samples (~5.3 ms)
    buildDeciTaps();
    const int irLen = (int)std::ceil(irSeconds * convRate_);
    parts_.clear();
    int rem = irLen, off = 0, s = baseBlock_, maxSO = 0;
    while (rem > 0) {
        for (int rep = 0; rep < 2 && rem > 0; ++rep) {
            int take = s;
            if (take > rem) {
                take = baseBlock_; // exact-sized tail (zero-padded)
                while (take < rem) take <<= 1;
            }
            Partition p;
            p.size = take; p.offset = off; p.fftLen = take * 2;
            p.irReL.assign((size_t)p.fftLen, 0.0f);
            p.irImL.assign((size_t)p.fftLen, 0.0f);
            p.irReR.assign((size_t)p.fftLen, 0.0f);
            p.irImR.assign((size_t)p.fftLen, 0.0f);
            p.accL.assign((size_t)take, 0.0f);
            p.accR.assign((size_t)take, 0.0f);
            parts_.push_back(std::move(p));
            maxSO = std::max(maxSO, take + off);
            off += take; rem -= take;
        }
        s <<= 1;
    }
    outLen_ = maxSO + 4 * baseBlock_;
    outBufL_.assign((size_t)outLen_, 0.0f);
    outBufR_.assign((size_t)outLen_, 0.0f);
    inStageL_.assign((size_t)baseBlock_, 0.0f);
    inStageR_.assign((size_t)baseBlock_, 0.0f);
    outStageL_.assign((size_t)baseBlock_, 0.0f);
    outStageR_.assign((size_t)baseBlock_, 0.0f);
    int maxFft = 0;
    for (auto& p : parts_) maxFft = std::max(maxFft, p.fftLen);
    tmpRe_.assign((size_t)maxFft, 0.0f);
    tmpIm_.assign((size_t)maxFft, 0.0f);
    accRe_.assign((size_t)maxFft, 0.0f);
    accIm_.assign((size_t)maxFft, 0.0f);
    rsDeciHistL_.assign(64, 0.0f); rsDeciHistR_.assign(64, 0.0f);
    rsInterHistL_.assign(64, 0.0f); rsInterHistR_.assign(64, 0.0f);
    cvInL_.assign(4096, 0.0f); cvInR_.assign(4096, 0.0f);
    cvOutL_.assign(4096, 0.0f); cvOutR_.assign(4096, 0.0f);
    reset();
}

void PartitionedConvolver::reset() {
    for (auto& p : parts_) {
        std::fill(p.accL.begin(), p.accL.end(), 0.0f);
        std::fill(p.accR.begin(), p.accR.end(), 0.0f);
        p.accFill = 0;
        // IR spectra are preserved.
    }
    std::fill(outBufL_.begin(), outBufL_.end(), 0.0f);
    std::fill(outBufR_.begin(), outBufR_.end(), 0.0f);
    std::fill(inStageL_.begin(), inStageL_.end(), 0.0f);
    std::fill(inStageR_.begin(), inStageR_.end(), 0.0f);
    std::fill(outStageL_.begin(), outStageL_.end(), 0.0f);
    std::fill(outStageR_.begin(), outStageR_.end(), 0.0f);
    totalIn_ = 0; inFill_ = 0; outPos_ = 0; outStaged_ = false;
    std::fill(rsDeciHistL_.begin(), rsDeciHistL_.end(), 0.0f);
    std::fill(rsDeciHistR_.begin(), rsDeciHistR_.end(), 0.0f);
    std::fill(rsInterHistL_.begin(), rsInterHistL_.end(), 0.0f);
    std::fill(rsInterHistR_.begin(), rsInterHistR_.end(), 0.0f);
    rsDeciPosL_ = rsDeciPosR_ = rsInterPosL_ = rsInterPosR_ = 0;
    rsPhL_ = 0; rsPhR_ = 0;
}

void PartitionedConvolver::setIR(const float* irL, const float* irR, int numSamples) {
    if (!irL || !irR || numSamples <= 0 || parts_.empty() || deciTaps_.empty()) return;
    const int convLen = numSamples / 2;
    // Offline 2x decimation (even phase, matching the runtime path).
    std::vector<float> dL((size_t)convLen), dR((size_t)convLen);
    for (int i = 0; i < convLen; ++i) {
        double aL = 0.0, aR = 0.0;
        for (int k = 0; k < 64; ++k) {
            const int idx = 2 * i - k;
            const float sL = (idx >= 0) ? irL[idx] : 0.0f;
            const float sR = (idx >= 0) ? irR[idx] : 0.0f;
            aL += (double)deciTaps_[(size_t)k] * sL;
            aR += (double)deciTaps_[(size_t)k] * sR;
        }
        dL[(size_t)i] = (float)aL; dR[(size_t)i] = (float)aR;
    }
    for (auto& p : parts_) {
        for (int ch = 0; ch < 2; ++ch) {
            const float* src = (ch == 0) ? dL.data() : dR.data();
            for (int i = 0; i < p.size; ++i) {
                const int si = p.offset + i;
                // x2: the conv-rate discrete sum needs the rate-ratio scaling
                // to match a host-rate convolution's amplitude.
                tmpRe_[(size_t)i] = (si < convLen) ? 2.0f * src[si] : 0.0f;
                tmpIm_[(size_t)i] = 0.0f;
            }
            for (int i = p.size; i < p.fftLen; ++i)
                tmpRe_[(size_t)i] = tmpIm_[(size_t)i] = 0.0f;
            fft(tmpRe_.data(), tmpIm_.data(), p.fftLen, false);
            float* dRe = (ch == 0) ? p.irReL.data() : p.irReR.data();
            float* dIm = (ch == 0) ? p.irImL.data() : p.irImR.data();
            for (int i = 0; i < p.fftLen; ++i) {
                dRe[i] = tmpRe_[(size_t)i]; dIm[i] = tmpIm_[(size_t)i];
            }
        }
    }
    reset(); // clear state; the fresh IR is preserved
}

void PartitionedConvolver::convolverTick() {
    const int B = baseBlock_;
    for (auto& p : parts_) {
        float* aL = p.accL.data() + p.accFill;
        float* aR = p.accR.data() + p.accFill;
        for (int i = 0; i < B; ++i) { aL[i] = inStageL_[(size_t)i]; aR[i] = inStageR_[(size_t)i]; }
        p.accFill += B;
        if (p.accFill < p.size) continue;
        const int N = p.fftLen;
        const long long outBase = totalIn_ + B - p.size + p.offset;
        for (int ch = 0; ch < 2; ++ch) {
            const float* acc = (ch == 0) ? p.accL.data() : p.accR.data();
            for (int i = 0; i < p.size; ++i) { tmpRe_[(size_t)i] = acc[i]; tmpIm_[(size_t)i] = 0.0f; }
            for (int i = p.size; i < N; ++i) tmpRe_[(size_t)i] = tmpIm_[(size_t)i] = 0.0f;
            fft(tmpRe_.data(), tmpIm_.data(), N, false);
            const float* hRe = (ch == 0) ? p.irReL.data() : p.irReR.data();
            const float* hIm = (ch == 0) ? p.irImL.data() : p.irImR.data();
            for (int i = 0; i < N; ++i) {
                const float xr = tmpRe_[(size_t)i], xi = tmpIm_[(size_t)i];
                accRe_[(size_t)i] = xr * hRe[i] - xi * hIm[i];
                accIm_[(size_t)i] = xr * hIm[i] + xi * hRe[i];
            }
            fft(accRe_.data(), accIm_.data(), N, true);
            float* outBuf = (ch == 0) ? outBufL_.data() : outBufR_.data();
            const int twoS = 2 * p.size;
            for (int i = 0; i < twoS; ++i) {
                const int idx = (int)((outBase + i) % outLen_);
                outBuf[idx] += accRe_[(size_t)i];
            }
        }
        p.accFill = 0;
    }
    totalIn_ += B;
    const long long start = totalIn_ - B;
    for (int i = 0; i < B; ++i) {
        const int idx = (int)((start + i) % outLen_);
        outStageL_[(size_t)i] = outBufL_[(size_t)idx]; outBufL_[(size_t)idx] = 0.0f;
        outStageR_[(size_t)i] = outBufR_[(size_t)idx]; outBufR_[(size_t)idx] = 0.0f;
    }
    outPos_ = 0;
    outStaged_ = true;
}

// Host-rate wrapper: resamples around the conv-rate core.
void PartitionedConvolver::processBlock(const float* inL, const float* inR,
                                        float* outL, float* outR, int n) {
    if (n <= 0) return;
    const int mMax = n / 2 + 8;
    if ((int)cvInL_.size() < mMax) {
        cvInL_.assign((size_t)mMax, 0.0f); cvInR_.assign((size_t)mMax, 0.0f);
        cvOutL_.assign((size_t)mMax, 0.0f); cvOutR_.assign((size_t)mMax, 0.0f);
    }
    const int mL = decimate(inL, cvInL_.data(), n, 0);
    const int mR = decimate(inR, cvInR_.data(), n, 1);
    const int m = std::min(mL, mR);
    processCore(cvInL_.data(), cvInR_.data(), cvOutL_.data(), cvOutR_.data(), m);
    // Back to host rate, straight into the caller's buffers.
    const int kL = interpolate(cvOutL_.data(), outL, m, 0);
    const int kR = interpolate(cvOutR_.data(), outR, m, 1);
    const int k = std::min(kL, kR);
    for (int i = k; i < n; ++i) outL[i] = outR[i] = 0.0f; // odd-n tail: silence
}

// Conv-rate core: stages arbitrary input lengths to baseBlock_ multiples.
void PartitionedConvolver::processCore(const float* inL, const float* inR,
                                       float* outL, float* outR, int m) {
    const int B = baseBlock_;
    int ii = 0, oi = 0;
    while (oi < m) {
        if (outStaged_ && outPos_ < B) {
            outL[oi] = outStageL_[(size_t)outPos_];
            outR[oi] = outStageR_[(size_t)outPos_];
            ++outPos_; ++oi;
            if (outPos_ >= B) outStaged_ = false;
            continue;
        }
        if (ii < m) {
            const int take = std::min(B - inFill_, m - ii);
            for (int i = 0; i < take; ++i) {
                inStageL_[(size_t)(inFill_ + i)] = inL[ii + i];
                inStageR_[(size_t)(inFill_ + i)] = inR[ii + i];
            }
            inFill_ += take; ii += take;
            if (inFill_ == B) { convolverTick(); inFill_ = 0; }
            continue;
        }
        outL[oi] = 0.0f; outR[oi] = 0.0f; ++oi; // end of stream: pad zeros
    }
}

int PartitionedConvolver::blockSize() const { return baseBlock_; }
int PartitionedConvolver::latencySamples() const {
    // 31.5 (input decimator) + 31.5 (IR decimation, baked into the IR in
    // setIR) + 0 (zero-latency direct partition) + 31.5 (interpolator).
    return 95;
}

// ---------------------------------------------------------------------------
// Chunk D: resamplers, IR build, beat scheduler, drum voices, drum bus.
// ---------------------------------------------------------------------------



void Sg9Dsp::buildSpaceIR() {
    // 30 s stereo noise IR x (1-t)^0.68 (web makeIR). Deterministic seed so
    // every instance builds the identical room.
    const int irLen = (int)std::ceil(kSpaceIrSeconds * sampleRate_);
    std::vector<float> irL((size_t)irLen), irR((size_t)irLen);
    SeededRng irRng(0xC0FFEEu);
    for (int i = 0; i < irLen; ++i) {
        const float t = (float)i / (float)(irLen - 1);
        const float e = std::pow(1.0f - t, kSpaceIrDecayExp);
        irL[(size_t)i] = (irRng.nextFloat() * 2.0f - 1.0f) * e;
        irR[(size_t)i] = (irRng.nextFloat() * 2.0f - 1.0f) * e;
    }
    convolver_.setIR(irL.data(), irR.data(), irLen);
}

int PartitionedConvolver::decimate(const float* in, float* out, int n, int ch) {
    std::vector<float>& hist = (ch == 0) ? rsDeciHistL_ : rsDeciHistR_;
    int& pos = (ch == 0) ? rsDeciPosL_ : rsDeciPosR_;
    int& phase = (ch == 0) ? rsPhL_ : rsPhR_;
    int m = 0;
    for (int i = 0; i < n; ++i) {
        hist[(size_t)pos] = in[i];
        pos = (pos + 1) & 63;
        if (((phase++) & 1) == 0) {
            double acc = 0.0;
            for (int k = 0; k < 64; ++k)
                acc += (double)deciTaps_[(size_t)k] *
                       hist[(size_t)((pos - 1 - k) & 63)];
            out[m++] = (float)acc;
        }
    }
    return m;
}

int PartitionedConvolver::interpolate(const float* in, float* out, int n, int ch) {
    std::vector<float>& hist = (ch == 0) ? rsInterHistL_ : rsInterHistR_;
    int& pos = (ch == 0) ? rsInterPosL_ : rsInterPosR_;
    for (int i = 0; i < n; ++i) {
        hist[(size_t)pos] = in[i];
        pos = (pos + 1) & 63;
        double aE = 0.0, aO = 0.0;
        for (int k = 0; k < 32; ++k) {
            const float h = hist[(size_t)((pos - 1 - k) & 63)];
            aE += (double)deciTaps_[(size_t)(2 * k)] * h;
            aO += (double)deciTaps_[(size_t)(2 * k + 1)] * h;
        }
        out[2 * i] = (float)(2.0 * aE); // x2 compensates zero-insertion
        out[2 * i + 1] = (float)(2.0 * aO);
    }
    return 2 * n;
}

// --- beat scheduler --------------------------------------------------------
// One shared clock drives BEAT and PULSE (web schedulePulseLookahead). Beats
// are placed sample-accurately: drum hits and pulse toms carry the beat's
// sub-block offset as a pre-delay.
void Sg9Dsp::advanceScheduler(int n) {
    if (activeVoice_ < 0) return;
    const float dt = (float)invSampleRate_;
    const float blockEnd = schedClock_ + (float)n * dt;
    int pat = params_.beatPattern;
    if (pat < 0) pat = 0;
    if (pat >= kNumBeatPatterns) pat = kNumBeatPatterns - 1;
    const BeatPattern& P = kBeatPatterns[pat];
    int guard = 0;
    while (nextBeatAt_ < blockEnd && guard++ < 1024) {
        float delaySec = nextBeatAt_ - schedClock_;
        if (delaySec < 0.0f) delaySec = 0.0f;
        const bool downbeat = (beatInPattern_ == 0);
        firePatternBeat(P, beatInPattern_, downbeat, delaySec);
        onBeat(beatInPattern_, downbeat, delaySec);
        if (++beatInPattern_ >= P.beats) beatInPattern_ = 0;
        nextBeatAt_ += beatSec_;
    }
    schedClock_ = blockEnd;
}

void Sg9Dsp::firePatternBeat(const BeatPattern& P, int beat, bool downbeat,
                             float delaySec) {
    for (int i = 0; i < P.bassCount; ++i) if (P.bass[i] == beat) {
        fireBeatHit(kHitDum, downbeat ? kDumLevelDown : kDumLevel,
                    0.0f, downbeat, delaySec);
        fireBeatHit(kHitKick, downbeat ? kKickLevelDown : kKickLevel,
                    0.0f, downbeat, delaySec);
    }
    for (int i = 0; i < P.bayanCount; ++i) if (P.bayan[i] == beat)
        fireBeatHit(kHitBayan, downbeat ? kBayanLevelDown : kBayanLevel,
                    kBayanPan, downbeat, delaySec);
    for (int i = 0; i < P.dayanCount; ++i) if (P.dayan[i] == beat) {
        // Every other written TAK speaks (web woodblockOccurrence % 2).
        if (!dayanAlt_) {
            fireBeatHit(kHitTak, downbeat ? kTakLevelDown : kTakLevel,
                        kTakPan, downbeat, delaySec);
            scheduleWoodTaps();
        }
        dayanAlt_ = !dayanAlt_;
    }
    for (int i = 0; i < P.handCount; ++i) if (P.hand[i] == beat)
        fireBeatHit(kHitHand, kHandLevel, (beat & 1) ? 0.34f : -0.32f,
                    downbeat, delaySec);
}

// --- drum voices -----------------------------------------------------------
static float xorshiftNoise(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    s |= 1u;
    return (float)(int32_t)s * (1.0f / 2147483648.0f);
}
static float expSeg(float t, float t0, float v0, float t1, float v1) {
    if (t <= t0) return v0;
    if (t >= t1) return v1;
    const float k = (t - t0) / (t1 - t0);
    return v0 * std::pow(v1 / v0, k);
}

void Sg9Dsp::fireBeatHit(int kind, float gain, float pan, bool /*downbeat*/,
                         float delaySec) {
    if (activeVoice_ < 0) return;
    const float f = kChakras[activeVoice_].freq;
    BeatHit* slot = nullptr;
    for (auto& h : beatHits_) if (!h.active) { slot = &h; break; }
    if (!slot) { // steal the oldest hit
        slot = &beatHits_[0];
        for (auto& h : beatHits_) if (h.t > slot->t) slot = &h;
    }
    slot->active = true;
    slot->kind = (uint8_t)kind;
    slot->t = -delaySec; // sample-accurate beat start within the block
    slot->gain = gain;
    slot->pan = pan;
    slot->nzState = rng_.next() | 1u;
    slot->st1 = slot->st2 = slot->st3 = 0.0f;
    slot->filt.reset(); slot->filt2.reset();
    slot->oscA.setTable(&sineTable_); slot->oscB.setTable(&sineTable_);
    slot->oscA.reset(0.0f); slot->oscB.reset(0.0f);
    switch (kind) {
    case kHitDum: {
        const float root = foldFrequency(f * 0.125f, 48.0f, 74.0f);
        slot->freq = root * kDumPitchStartRatio;
        slot->freqEnd = root;
        slot->pitchSec = kDumPitchSec;
        slot->dur = std::min(1.75f, std::max(0.82f, beatSec_ * 0.72f)) + 0.16f;
        slot->filt.setLowpass(std::min(360.0f, std::max(145.0f, root * 3.1f)), 0.35f);
        slot->oscB.setFreq(root * 2.0f); // restrained 2nd harmonic
        break;
    }
    case kHitKick: {
        slot->freq = kKickStartHz; // 76 -> 48 over .16 s, then -> 38
        slot->freqEnd = kKickEndHz;
        slot->pitchSec = kKickPitchSec;
        slot->dur = std::min(1.35f, std::max(0.62f, beatSec_ * 0.56f)) + 0.08f;
        slot->filt.setLowpass(kKickLpHz, 0.22f);
        break;
    }
    case kHitBayan: {
        const float root = foldFrequency(f * 0.125f, 46.0f, 82.0f);
        slot->freq = root * kBayanPitchStartRatio;
        slot->freqEnd = root;
        slot->pitchSec = kBayanPitchSec;
        slot->dur = kBayanDecaySec + 0.06f;
        slot->filt.setLowpass(kBayanLpHz, 0.28f);
        slot->oscB.setFreq(root * 0.5f); // sub weight
        break;
    }
    case kHitTak: {
        const float hz = foldFrequency(f * 1.8f, 620.0f, 1050.0f);
        slot->freq = hz * kTakPitchStartRatio;
        slot->freqEnd = hz;
        slot->pitchSec = kTakPitchSec;
        slot->dur = 0.32f;
        slot->filt.setBandpass(hz, kTakBpQ);
        slot->filt2.setBandpass(kTakFleckBpHz, kTakFleckBpQ);
        break;
    }
    case kHitHand:
    default: {
        const float hz = foldFrequency(f * 0.42f, 110.0f, 190.0f);
        slot->freq = hz * kHandPitchStartRatio;
        slot->freqEnd = hz;
        slot->pitchSec = 0.12f;
        slot->dur = 0.42f;
        slot->filt.setLowpass(kHandLpHz, 0.3f);
        slot->filt2.setBandpass(2400.0f, 0.5f); // jingle halo (HP1450->LP3800)
        break;
    }
    }
}

float Sg9Dsp::beatHitSample(BeatHit& h, float t, float shakerLpAlpha, float hpAlpha) {
    switch (h.kind) {
    case kHitDum: {
        const float k = std::min(t / h.pitchSec, 1.0f);
        h.oscA.setFreq(h.freq * std::pow(h.freqEnd / h.freq, k));
        const float dec = h.dur - 0.16f;
        float env;
        if (t < 0.026f) env = 0.0001f + (h.gain - 0.0001f) * (t / 0.026f);
        else if (t < dec * 0.48f) env = expSeg(t, 0.026f, h.gain, dec * 0.48f, h.gain * 0.38f);
        else env = expSeg(t, dec * 0.48f, h.gain * 0.38f, dec, 0.0001f);
        const float s = h.oscA.tick() + h.oscB.tick() * kDumHarm2Gain;
        return h.filt.process(s) * env;
    }
    case kHitKick: {
        const float dec = h.dur - 0.08f;
        float fr;
        if (t < h.pitchSec) fr = h.freq * std::pow(kKickMidHz / h.freq, t / h.pitchSec);
        else fr = kKickMidHz * std::pow(h.freqEnd / kKickMidHz,
                    (t - h.pitchSec) / std::max(0.01f, dec * 0.72f - h.pitchSec));
        h.oscA.setFreq(fr);
        float env;
        if (t < 0.018f) env = 0.0001f + (h.gain - 0.0001f) * (t / 0.018f);
        else if (t < dec * 0.48f) env = expSeg(t, 0.018f, h.gain, dec * 0.48f, h.gain * 0.32f);
        else env = expSeg(t, dec * 0.48f, h.gain * 0.32f, dec, 0.0001f);
        return h.filt.process(h.oscA.tick()) * env;
    }
    case kHitBayan: {
        const float k = std::min(t / h.pitchSec, 1.0f);
        h.oscA.setFreq(h.freq * std::pow(h.freqEnd / h.freq, k));
        float env;
        if (t < 0.046f) env = 0.0001f + (h.gain - 0.0001f) * (t / 0.046f);
        else if (t < 0.38f) env = expSeg(t, 0.046f, h.gain, 0.38f, h.gain * 0.32f);
        else env = expSeg(t, 0.38f, h.gain * 0.32f, kBayanDecaySec, 0.0001f);
        const float s = h.oscA.tick() + h.oscB.tick() * kBayanSubGain;
        return h.filt.process(s) * env;
    }
    case kHitTak: {
        const float k = std::min(t / h.pitchSec, 1.0f);
        h.oscA.setFreq(h.freq * std::pow(h.freqEnd / h.freq, k));
        const float body = h.filt.process(h.oscA.tick()) *
            (t < kTakAttackSec ? 0.0001f + (h.gain * 0.88f - 0.0001f) * (t / kTakAttackSec)
                               : expSeg(t, kTakAttackSec, h.gain * 0.88f, kTakDecaySec, 0.0001f));
        const float fleck = h.filt2.process(xorshiftNoise(h.nzState)) *
            (t < 0.018f ? 0.0001f + (h.gain * kTakFleckGain - 0.0001f) * (t / 0.018f)
                        : expSeg(t, 0.018f, h.gain * kTakFleckGain, 0.13f, 0.0001f));
        return body + fleck;
    }
    case kHitHand:
    default: {
        const float k = std::min(t / h.pitchSec, 1.0f);
        h.oscA.setFreq(h.freq * std::pow(h.freqEnd / h.freq, k));
        const float body = h.filt.process(h.oscA.tick()) *
            (t < 0.018f ? 0.0001f + (h.gain - 0.0001f) * (t / 0.018f)
                        : expSeg(t, 0.018f, h.gain, kHandDecaySec, 0.0001f));
        const float jingle = h.filt2.process(xorshiftNoise(h.nzState)) *
            (t < 0.012f ? 0.0001f + (h.gain * kHandJingleGain - 0.0001f) * (t / 0.012f)
                        : expSeg(t, 0.012f, h.gain * kHandJingleGain, 0.24f, 0.0001f));
        // Shaker: one-pole HP 240 -> one-pole LP (folded), states in st1..st3.
        const float x = xorshiftNoise(h.nzState);
        const float hpY = hpAlpha * (h.st2 + x - h.st3);
        h.st3 = x; h.st2 = hpY;
        h.st1 += shakerLpAlpha * (hpY - h.st1);
        const float shaker = h.st1 *
            (t < 0.05f ? (h.gain * 0.0f + kHandShakerLevel * (t / 0.05f))
                       : expSeg(t, 0.05f, kHandShakerLevel, kHandShakerDecaySec, 0.0001f));
        return body + jingle + shaker;
    }
    }
}

void Sg9Dsp::renderBeatHits(float* busL, float* busR, int n) {
    const float dt = (float)invSampleRate_;
    const float hpAlpha = std::exp(-2.0f * (float)kPi * kHandShakerHpHz * (float)invSampleRate_);
    for (auto& h : beatHits_) {
        if (!h.active) continue;
        float t = h.t;
        int start = 0;
        if (t < 0.0f) { // pre-delay: skip ahead sample-accurately
            const int skip = (int)(-t / dt);
            if (skip >= n) { h.t = t + (float)n * dt; continue; }
            start = skip;
            t += (float)start * dt;
        }
        const float a = (h.pan + 1.0f) * (float)(kPi / 4.0);
        const float gL = std::cos(a), gR = std::sin(a);
        float shakerLpAlpha = 0.0f;
        if (h.kind == kHitHand && activeVoice_ >= 0) {
            const float fcSh = foldFrequency(kChakras[activeVoice_].freq,
                                             480.0f, 820.0f);
            shakerLpAlpha = 1.0f - std::exp(-2.0f * (float)kPi * fcSh * dt);
        }
        for (int i = start; i < n; ++i) {
            t += dt;
            if (t < 0.0f) continue;
            if (t >= h.dur) { h.active = false; break; }
            const float s = beatHitSample(h, t, shakerLpAlpha, hpAlpha);
            busL[i] += s * gL;
            busR[i] += s * gR;
        }
        h.t = t;
    }
}

// --- woodblock ping-pong taps ----------------------------------------------
// Six feed-forward taps at eighth-note spacing, ZERO feedback by construction.
void Sg9Dsp::scheduleWoodTaps() {
    if (activeVoice_ < 0) return;
    const float hz = foldFrequency(kChakras[activeVoice_].freq * 1.8f,
                                   620.0f, 1050.0f);
    for (int i = 0; i < kWoodTaps; ++i) {
        WoodTap& tp = woodTaps_[i];
        tp.active = true;
        tp.sounding = false;
        tp.tapIdx = i;
        tp.delayLeft = eighthSec_ * (float)(i + 1); // 1/8 .. 6/8
        tp.freq = hz;
        tp.t = 0.0f;
        woodOsc_[i].setFreq(hz);
        woodOsc_[i].reset(0.0f);
        woodLp_[i].reset();
        woodLp_[i].setLowpass(920.0f - (float)i * 90.0f, 0.18f);
    }
}

void Sg9Dsp::renderWoodTaps(float* busL, float* busR, int n) {
    const float dt = (float)invSampleRate_;
    for (int i = 0; i < n; ++i) {
        float sL = 0.0f, sR = 0.0f;
        for (int k = 0; k < kWoodTaps; ++k) {
            WoodTap& tp = woodTaps_[k];
            if (!tp.active) continue;
            if (!tp.sounding) {
                tp.delayLeft -= dt;
                if (tp.delayLeft > 0.0f) continue;
                tp.sounding = true;
                tp.t = 0.0f;
                woodOsc_[k].reset(0.0f);
            }
            tp.t += dt;
            if (tp.t > 0.30f) { tp.active = false; continue; }
            const float env = (tp.t < 0.008f) ? (tp.t / 0.008f)
                : std::exp(-(tp.t - 0.008f) / 0.06f);
            const float s = woodLp_[k].process(woodOsc_[k].tick()) *
                            env * kWoodTapLevel[k];
            const float a = (kWoodTapPan[k] + 1.0f) * (float)(kPi / 4.0);
            sL += s * std::cos(a);
            sR += s * std::sin(a);
        }
        busL[i] += sL;
        busR[i] += sR;
    }
}

// --- drum bus ---------------------------------------------------------------
// Fader -> 4-beat pan orbit (depth .38) -> LP 2600 -> tanh(1.25x)/tanh(1.25)
// waveshaper -> feedforward compressor (thr -5 dB, knee 5, ratio 2.2,
// attack .008, release .075). (Web: drumTone -> rhythmShape -> rhythmComp.)
void Sg9Dsp::drumBusChain(float* busL, float* busR, int n) {
    const float dt = (float)invSampleRate_;
    const float driveN = 1.0f / std::tanh(kDrumDrive);
    const float attC = 1.0f - std::exp(-dt / kDrumCompAttSec);
    const float relC = 1.0f - std::exp(-dt / kDrumCompRelSec);
    for (int i = 0; i < n; ++i) {
        beatOrbitPhase_ += (float)(2.0 * kPi * dt / (4.0f * beatSec_));
        if (beatOrbitPhase_ > 2.0f * (float)kPi) beatOrbitPhase_ -= 2.0f * (float)kPi;
        const float pan = kDrumOrbitDepth * std::sin(beatOrbitPhase_);
        const float a = (pan + 1.0f) * (float)(kPi / 4.0);
        float l = busL[i] * std::cos(a), r = busR[i] * std::sin(a);
        l = drumToneLp_.process(l);
        r = drumToneLpR_.process(r);
        l = std::tanh(kDrumDrive * l) * driveN;
        r = std::tanh(kDrumDrive * r) * driveN;
        const float peak = std::max(std::fabs(l), std::fabs(r));
        drumCompEnv_ += (peak - drumCompEnv_) * (peak > drumCompEnv_ ? attC : relC);
        float gr = 1.0f;
        const float ov = 20.0f * std::log10(drumCompEnv_ + 1e-9f) - kDrumCompThrDb;
        if (ov > 0.0f) {
            const float gdb = (1.0f / kDrumCompRatio - 1.0f) *
                (ov < kDrumCompKneeDb ? ov * ov / (2.0f * kDrumCompKneeDb)
                                      : ov - kDrumCompKneeDb * 0.5f);
            gr = std::pow(10.0f, gdb / 20.0f);
        }
        busL[i] = l * gr;
        busR[i] = r * gr;
    }
}

// ---------------------------------------------------------------------------
// Chunk D: space, master, process() glue.
// ---------------------------------------------------------------------------

void Sg9Dsp::clearBuses(int n) {
    std::fill(busDroneL_.begin(), busDroneL_.begin() + n, 0.0f);
    std::fill(busDroneR_.begin(), busDroneR_.begin() + n, 0.0f);
    std::fill(busPadsL_.begin(), busPadsL_.begin() + n, 0.0f);
    std::fill(busPadsR_.begin(), busPadsR_.begin() + n, 0.0f);
    std::fill(busPulseL_.begin(), busPulseL_.begin() + n, 0.0f);
    std::fill(busPulseR_.begin(), busPulseR_.begin() + n, 0.0f);
    std::fill(busDrumsL_.begin(), busDrumsL_.begin() + n, 0.0f);
    std::fill(busDrumsR_.begin(), busDrumsR_.begin() + n, 0.0f);
    std::fill(busSpaceSendL_.begin(), busSpaceSendL_.begin() + n, 0.0f);
    std::fill(busSpaceSendR_.begin(), busSpaceSendR_.begin() + n, 0.0f);
    std::fill(busSpaceDelayL_.begin(), busSpaceDelayL_.begin() + n, 0.0f);
    std::fill(busSpaceDelayR_.begin(), busSpaceDelayR_.begin() + n, 0.0f);
    std::fill(busMixL_.begin(), busMixL_.begin() + n, 0.0f);
    std::fill(busMixR_.begin(), busMixR_.begin() + n, 0.0f);
}

// Drone delay: post-fader drone bus -> .56 s delay, LP 760, in .34 / out .24
// -> space delay return (web: droneDelayIn -> droneDelay -> droneDelayTone ->
// droneDelayOut -> spaceDelayWet).
void Sg9Dsp::renderDroneDelayToSpace(float* spaceDelayL, float* spaceDelayR, int n) {
    for (int i = 0; i < n; ++i) {
        droneDelay_.write(busDroneL_[i] * kDroneDelayIn);
        droneDelayR_.write(busDroneR_[i] * kDroneDelayIn);
        spaceDelayL[i] += droneDelayLp_.process(droneDelay_.read()) * kDroneDelayOut;
        spaceDelayR[i] += droneDelayLpR_.process(droneDelayR_.read()) * kDroneDelayOut;
    }
}

// Space: 30 s non-uniform convolver on the post-fader send bus (+ delayBloom
// .46 of the previous block's delay return into the reverb input), mid/side
// widening (mid .42/.42, side +/-1.20), decorrelation delays, return EQ,
// fader-scaled returns (reverb x.58 / delay x.50) into the mix.
void Sg9Dsp::renderSpace(float* spaceSendL, float* spaceSendR,
                         float* spaceDelayL, float* spaceDelayR,
                         float* mixL, float* mixR, int n) {
    const float spaceF = params_.faderSpace;
    const float revG = spaceF * kSpaceReverbReturn;
    const float dlyG = spaceF * kSpaceDelayReturn;
    // Convolver input = send + bloom of the previous delay return.
    // (processBlock is host-rate; it resamples around its 24 kHz core.)
    float* cinL = convInL_.data();
    float* cinR = convInR_.data();
    for (int i = 0; i < n; ++i) {
        cinL[i] = spaceSendL[i] + kDelayBloom * bloomL_;
        cinR[i] = spaceSendR[i] + kDelayBloom * bloomR_;
    }
    float* wetL = convOutL_.data();
    float* wetR = convOutR_.data();
    convolver_.processBlock(cinL, cinR, wetL, wetR, n);
    double dlySumL = 0.0, dlySumR = 0.0;
    for (int i = 0; i < n; ++i) {
        const float wl = wetL[i], wr = wetR[i];
        // Mid/side: mid .42/.42, side +1.20/-1.20 (240% width).
        const float mid = kSpaceMidGain * (wl + wr);
        const float side = kSpaceSideGain * (wl - wr);
        float oL = mid + side, oR = mid - side;
        // Strong decorrelation by unequal delays.
        decorrL_.write(oL); decorrR_.write(oR);
        oL = decorrL_.read(); oR = decorrR_.read();
        const float dL = spaceDelayL[i] * dlyG;
        const float dR = spaceDelayR[i] * dlyG;
        dlySumL += dL; dlySumR += dR;
        float sL = oL * revG + dL;
        float sR = oR * revG + dR;
        for (int e = 0; e < 4; ++e) {
            sL = spaceEqL_[e].process(sL);
            sR = spaceEqR_[e].process(sR);
        }
        mixL[i] += sL;
        mixR[i] += sR;
    }
    bloomL_ = (float)(dlySumL / (double)std::max(n, 1));
    bloomR_ = (float)(dlySumR / (double)std::max(n, 1));
}

void Sg9Dsp::renderFeltBass(float* mixL, float* mixR, int n) {
    for (int i = 0; i < n; ++i) {
        const float s = feltLp_.process(feltOsc_.tick()) * kFeltGain;
        mixL[i] += s;
        mixR[i] += s;
    }
}

// Master: breath bed on drone+pads, felt bass, post-fader space sends,
// gentle tanh safety, peak meters. No compressor on drone/pads; no noise.
void Sg9Dsp::renderMaster(float* mixL, float* mixR, int n) {
    const float br = breath01(breathPhase_); // shared 10.9 s cycle (chunk C)
    const float bed = kBreathBase + kBreathDepth * br;
    float* dL = busDroneL_.data(); float* dR = busDroneR_.data();
    float* pL = busPadsL_.data();  float* pR = busPadsR_.data();
    float* uL = busPulseL_.data(); float* uR = busPulseR_.data();
    float* drL = busDrumsL_.data(); float* drR = busDrumsR_.data();
    for (int i = 0; i < n; ++i) {
        mixL[i] = (dL[i] + pL[i]) * bed + uL[i] + drL[i];
        mixR[i] = (dR[i] + pR[i]) * bed + uR[i] + drR[i];
    }
    renderFeltBass(mixL, mixR, n);
    // Post-fader sends (web: every room send originates after its fader).
    float* sSL = busSpaceSendL_.data(); float* sSR = busSpaceSendR_.data();
    for (int i = 0; i < n; ++i) {
        sSL[i] = dL[i] + pL[i] + uL[i] + drL[i];
        sSR[i] = dR[i] + pR[i] + uR[i] + drR[i];
    }
    renderDroneDelayToSpace(busSpaceDelayL_.data(), busSpaceDelayR_.data(), n);
    renderSpace(sSL, sSR, busSpaceDelayL_.data(), busSpaceDelayR_.data(),
                mixL, mixR, n);
    // Gentle tanh safety + peak meters.
    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i) {
        mixL[i] = std::tanh(mixL[i]);
        mixR[i] = std::tanh(mixR[i]);
        pkL = std::max(pkL, std::fabs(mixL[i]));
        pkR = std::max(pkR, std::fabs(mixR[i]));
    }
    outMeterL_ = std::max(pkL, outMeterL_ * 0.94f);
    outMeterR_ = std::max(pkR, outMeterR_ * 0.94f);
}

void Sg9Dsp::process(float* outL, float* outR, int numSamples) {
    int done = 0;
    while (done < numSamples) {
        const int n = std::min(maxBlock_, numSamples - done);
        clearBuses(n);
        float* dL = busDroneL_.data(); float* dR = busDroneR_.data();
        float* pL = busPadsL_.data();  float* pR = busPadsR_.data();
        float* uL = busPulseL_.data(); float* uR = busPulseR_.data();
        float* drL = busDrumsL_.data(); float* drR = busDrumsR_.data();
        const bool voiceOn = (activeVoice_ >= 0 || releasing_);
        if (voiceOn) {
            // Drone (1.4x pre-gain is inside renderDrone) -> DRONE fader.
            renderDrone(dL, dR, n);
            const float gD = params_.faderDrone;
            for (int i = 0; i < n; ++i) { dL[i] *= gD; dR[i] *= gD; }
            // Pads -> PADS fader -> post-fader pad delays.
            renderPads(pL, pR, n);
            const float gP = params_.faderPads;
            for (int i = 0; i < n; ++i) { pL[i] *= gP; pR[i] *= gP; }
            renderPadDelays(pL, pR, n);
            // Scheduler first (sample-accurate beat offsets), then voices.
            advanceScheduler(n);
            renderPulseBeat(uL, uR, drL, drR, n);
            const float gU = params_.faderPulse;
            for (int i = 0; i < n; ++i) { uL[i] *= gU; uR[i] *= gU; }
            renderBeatHits(drL, drR, n);
            renderWoodTaps(drL, drR, n);
            const float gB = params_.faderBeat;
            for (int i = 0; i < n; ++i) { drL[i] *= gB; drR[i] *= gB; }
            drumBusChain(drL, drR, n);
        }
        float* mL = busMixL_.data();
        float* mR = busMixR_.data();
        renderMaster(mL, mR, n);
        for (int i = 0; i < n; ++i) {
            outL[done + i] = mL[i];
            outR[done + i] = mR[i];
        }
        done += n;
    }
}

} // namespace sg9
