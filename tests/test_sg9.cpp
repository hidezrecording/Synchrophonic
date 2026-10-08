// SG-9 native DSP tests — NEW web-engine port.
//
// Covers: chakra triggering, drone 1.4x pre-gain, pulse tom grace period,
// pad voicings, beat patterns, tempo sources x speeds, drone tones,
// harmony-follow FFT, determinism, 30 s NaN/peak, voice switching/stop,
// convolution tail decay.
//
// Build: g++ -std=c++17 -O2 -o build/test_sg9 tests/test_sg9.cpp dsp/Sg9Dsp.cpp
// (from ~/workspace/plugins/sg9)

#include "../dsp/Sg9Dsp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const double kSr = 48000.0;
int gChecks = 0, gFails = 0;

#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFails; \
    std::printf("FAIL [%s:%d] ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

sg9::Sg9Dsp makeDsp(const sg9::Sg9Params& p) {
    sg9::Sg9Dsp d;
    d.prepare(kSr);
    d.reset();
    d.setParams(p);
    return d;
}

struct Render {
    std::vector<float> L, R;
    int n = 0;
};

Render render(sg9::Sg9Dsp& d, double seconds) {
    Render r;
    r.n = (int)(seconds * kSr);
    r.L.assign((size_t)r.n, 0.0f);
    r.R.assign((size_t)r.n, 0.0f);
    // Host-like block sizes.
    int done = 0;
    const int blk = 512;
    while (done < r.n) {
        int n = std::min(blk, r.n - done);
        d.process(r.L.data() + done, r.R.data() + done, n);
        done += n;
    }
    return r;
}

bool finiteBuf(const Render& r) {
    for (int i = 0; i < r.n; ++i)
        if (!std::isfinite(r.L[i]) || !std::isfinite(r.R[i])) return false;
    return true;
}

double rmsWin(const Render& r, double t0, double t1) {
    int a = std::max(0, (int)(t0 * kSr)), b = std::min(r.n, (int)(t1 * kSr));
    if (b <= a) return 0.0;
    double s = 0.0;
    for (int i = a; i < b; ++i) s += 0.5 * ((double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i]);
    return std::sqrt(s / (b - a));
}

double peakWin(const Render& r, double t0, double t1) {
    int a = std::max(0, (int)(t0 * kSr)), b = std::min(r.n, (int)(t1 * kSr));
    double pk = 0.0;
    for (int i = a; i < b; ++i) {
        double v = 0.5 * std::fabs(r.L[i]) + 0.5 * std::fabs(r.R[i]);
        if (v > pk) pk = v;
    }
    return pk;
}

// Goertzel power at freq over [t0,t1), mono mix.
double goertzel(const Render& r, double t0, double t1, double freq) {
    int a = std::max(0, (int)(t0 * kSr)), b = std::min(r.n, (int)(t1 * kSr));
    const double w = 2.0 * 3.141592653589793 * freq / kSr, cw = std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = a; i < b; ++i) {
        double x = 0.5 * (r.L[i] + r.R[i]);
        s0 = x + 2 * cw * s1 - s2; s2 = s1; s1 = s0;
    }
    double p = s1 * s1 + s2 * s2 - 2 * cw * s1 * s2;
    return p / std::max(1, b - a);
}

sg9::Sg9Params soloParams() {
    sg9::Sg9Params p;
    p.faderDrone = 0; p.faderPulse = 0; p.faderPads = 0;
    p.faderBeat = 0;  p.faderSpace = 0;
    return p;
}

// ---------------------------------------------------------------------------
// 1. All 9 chakras trigger and render non-silent stereo, no NaN/Inf.
// ---------------------------------------------------------------------------
void testChakras() {
    for (int i = 0; i < sg9::kNumChakras; ++i) {
        sg9::Sg9Params p; // defaults
        sg9::Sg9Dsp d = makeDsp(p);
        d.triggerVoice(i);
        Render r = render(d, 6.0);
        CHECK(finiteBuf(r), "chakra %d: NaN/Inf in output", i);
        double e = rmsWin(r, 4.5, 6.0);
        CHECK(e > 0.02, "chakra %d (%s): too quiet rms=%.4f",
              i, sg9::kChakras[i].name, e);
        CHECK(d.getActiveTrigger() == i, "chakra %d: active trigger wrong", i);
    }
    std::printf("testChakras done\n");
}

// ---------------------------------------------------------------------------
// 2. Drone pre-fader gain is 1.4x.
// ---------------------------------------------------------------------------
void testDronePreGain() {
    sg9::Sg9Dsp d0 = makeDsp(soloParams());
    CHECK(d0.getDronePreGain() == 1.4f, "default drone pre-gain != 1.4 (got %.3f)",
          d0.getDronePreGain());
    auto run = [](float pregain) {
        sg9::Sg9Params p = soloParams();
        p.faderDrone = 1.0f;
        sg9::Sg9Dsp d = makeDsp(p);
        d.debugSetDronePreGain(pregain);
        d.triggerVoice(5);
        Render r = render(d, 8.0);
        CHECK(finiteBuf(r), "pregain %.2f: NaN/Inf", pregain);
        return (double)d.getDroneBusRms(); // post-envelope bus: no felt floor
    };
    double e10 = run(1.0f), e14 = run(1.4f);
    double ratio = e14 / (e10 + 1e-12);
    CHECK(ratio > 1.39 && ratio < 1.41,
          "drone pre-gain ratio %.4f not 1.4 (e10=%.5f e14=%.5f)", ratio, e10, e14);
    std::printf("testDronePreGain done (ratio=%.3f)\n", ratio);
}

// ---------------------------------------------------------------------------
// 3. Pulse tom grace: cycles 0-2 silent, toms on cycles 3,5,7 (even cycle-3).
//    Full speed (121.4 BPM), Baladi (8 beats): cycle = 3.9526 s, first
//    downbeat at 0.14 s. Pulse bus isolated via faders (felt-bass floor).
// ---------------------------------------------------------------------------
void testPulseGrace() {
    sg9::Sg9Params p = soloParams();
    p.faderPulse = 1.0f;
    p.beatSpeed = 0;   // full
    p.beatPattern = 0; // baladi
    sg9::Sg9Dsp d = makeDsp(p);
    d.triggerVoice(5);
    const double beatSec = 60.0 / 121.4, cyc = 8.0 * beatSec;
    Render r = render(d, 0.14 + 8 * cyc + 1.0);
    CHECK(finiteBuf(r), "grace: NaN/Inf");
    bool ok = true;
    for (int c = 0; c < 8; ++c) {
        double t0 = 0.14 + c * cyc;
        double pk = peakWin(r, t0, t0 + 1.2);
        bool expectTom = (c >= 3) && ((c - 3) % 2 == 0);
        if (expectTom) {
            if (!(pk > 0.025)) { ok = false;
                std::printf("  cycle %d: expected tom, peak=%.4f\n", c, pk); }
        } else {
            if (!(pk < 0.025)) { ok = false;
                std::printf("  cycle %d: expected silence, peak=%.4f\n", c, pk); }
        }
    }
    CHECK(ok, "pulse grace pattern wrong (see per-cycle lines above)");
    std::printf("testPulseGrace done\n");
}

// ---------------------------------------------------------------------------
// 4. All 7 pad voicings render non-silent, no NaN/Inf.
// ---------------------------------------------------------------------------
void testPadVoicings() {
    for (int v = 0; v < sg9::kNumPadVoicings; ++v) {
        sg9::Sg9Params p = soloParams();
        p.faderPads = 1.0f;
        p.padVoicing = v;
        sg9::Sg9Dsp d = makeDsp(p);
        d.triggerVoice(5);
        Render r = render(d, 9.0);
        CHECK(finiteBuf(r), "pad voicing %d: NaN/Inf", v);
        double e = rmsWin(r, 6.0, 9.0);
        CHECK(e > 0.01, "pad voicing %d (%s): too quiet rms=%.4f",
              v, sg9::kPadVoicings[v].name, e);
    }
    std::printf("testPadVoicings done\n");
}

// ---------------------------------------------------------------------------
// 5. All 9 beat patterns schedule without NaN/Inf (2 cycles each, full speed).
// ---------------------------------------------------------------------------
void testBeatPatterns() {
    const int beats[9] = { 8, 8, 2, 8, 6, 7, 10, 12, 16 };
    for (int pat = 0; pat < sg9::kNumBeatPatterns; ++pat) {
        sg9::Sg9Params p = soloParams();
        p.faderBeat = 1.0f;
        p.beatSpeed = 0; // full: 121.4 BPM
        p.beatPattern = pat;
        sg9::Sg9Dsp d = makeDsp(p);
        d.triggerVoice(5);
        double cyc = beats[pat] * (60.0 / 121.4);
        Render r = render(d, 0.3 + 2 * cyc + 0.5);
        CHECK(finiteBuf(r), "beat pattern %d (%s): NaN/Inf",
              pat, sg9::kBeatPatterns[pat].name);
    }
    std::printf("testBeatPatterns done\n");
}

// ---------------------------------------------------------------------------
// 6. All 5 tempo sources x 3 speeds render without NaN/Inf.
// ---------------------------------------------------------------------------
void testTempoCombos() {
    for (int ts = 0; ts < 5; ++ts) {
        for (int sp = 0; sp < 3; ++sp) {
            sg9::Sg9Params p = soloParams();
            p.faderBeat = 1.0f;
            p.tempoSource = ts;
            p.beatSpeed = sp;
            sg9::Sg9Dsp d = makeDsp(p);
            d.triggerVoice(5);
            Render r = render(d, 3.0);
            CHECK(finiteBuf(r), "tempo src %d speed %d: NaN/Inf", ts, sp);
        }
    }
    std::printf("testTempoCombos done\n");
}

// ---------------------------------------------------------------------------
// 7. All 5 drone tones render non-silent.
// ---------------------------------------------------------------------------
void testDroneTones() {
    for (int t = 0; t < sg9::kNumDroneTones; ++t) {
        sg9::Sg9Params p = soloParams();
        p.faderDrone = 1.0f;
        p.droneTone = t;
        sg9::Sg9Dsp d = makeDsp(p);
        d.triggerVoice(5);
        Render r = render(d, 6.0);
        CHECK(finiteBuf(r), "drone tone %d: NaN/Inf", t);
        // Drone bus RMS (post-envelope, pre-fader): excludes the felt-bass
        // floor, so this genuinely measures the drone voice. Spec voice is
        // quiet (~-20 dB voicePeak); threshold is well below actual (~0.007).
        double e = d.getDroneBusRms();
        CHECK(e > 0.003, "drone tone %d (%s): drone bus too quiet rms=%.5f",
              t, sg9::kShrutiModes[t].name, e);
    }
    std::printf("testDroneTones done\n");
}

// ---------------------------------------------------------------------------
// 8. Harmony-follow: minorThird -> 6/5 energy dominates; majorThird -> 5/4;
//    octaves -> neither. Sacral base 417 Hz. Goertzel over [6.5,12] s.
// ---------------------------------------------------------------------------
void testHarmonyFollow() {
    const double base = 417.0;
    auto energy = [&](int voicing) {
        sg9::Sg9Params p = soloParams();
        p.faderDrone = 1.0f;
        p.padVoicing = voicing;
        sg9::Sg9Dsp d = makeDsp(p);
        d.triggerVoice(5);
        Render r = render(d, 12.0);
        CHECK(finiteBuf(r), "harmony voicing %d: NaN/Inf", voicing);
        double e65 = goertzel(r, 6.5, 12.0, base * 6.0 / 5.0);
        double e54 = goertzel(r, 6.5, 12.0, base * 5.0 / 4.0);
        return std::make_pair(e65, e54);
    };
    {
        auto [e65, e54] = energy(sg9::kPadMinorThird);
        double ratio = e65 / (e54 + 1e-30);
        std::printf("  minorThird: E65=%.3e E54=%.3e ratio=%.1f\n", e65, e54, ratio);
        CHECK(ratio > 100.0, "minorThird: 6/5 does not dominate (ratio=%.1f)", ratio);
    }
    {
        auto [e65, e54] = energy(sg9::kPadMajorThird);
        double ratio = e54 / (e65 + 1e-30);
        std::printf("  majorThird: E65=%.3e E54=%.3e ratio=%.1f\n", e65, e54, ratio);
        CHECK(ratio > 100.0, "majorThird: 5/4 does not dominate (ratio=%.1f)", ratio);
    }
    {
        auto [e65, e54] = energy(0); // octaves: neutral
        std::printf("  octaves: E65=%.3e E54=%.3e\n", e65, e54);
        CHECK(e65 < 1e-5 && e54 < 1e-5,
              "octaves: third bins not quiet (E65=%.3e E54=%.3e)", e65, e54);
    }
    std::printf("testHarmonyFollow done\n");
}

// ---------------------------------------------------------------------------
// 9. Determinism: two identical instances render bit-identical output.
// ---------------------------------------------------------------------------
void testDeterminism() {
    sg9::Sg9Params p; // defaults
    sg9::Sg9Dsp a = makeDsp(p), b = makeDsp(p);
    a.triggerVoice(5); b.triggerVoice(5);
    Render ra = render(a, 6.0), rb = render(b, 6.0);
    CHECK(ra.n == rb.n, "determinism: length mismatch");
    bool same = ra.n == rb.n;
    for (int i = 0; same && i < ra.n; ++i)
        if (ra.L[i] != rb.L[i] || ra.R[i] != rb.R[i]) same = false;
    CHECK(same, "determinism: renders differ");
    std::printf("testDeterminism done\n");
}

// ---------------------------------------------------------------------------
// 10. 30 s default render: no NaN/Inf, peak <= 1.0 (tanh), sane RMS.
// ---------------------------------------------------------------------------
void testThirtySeconds() {
    sg9::Sg9Params p; // defaults
    sg9::Sg9Dsp d = makeDsp(p);
    d.triggerVoice(5);
    Render r = render(d, 30.0);
    CHECK(finiteBuf(r), "30s: NaN/Inf");
    double pk = peakWin(r, 0.0, 30.0);
    CHECK(pk <= 1.0, "30s: peak %.4f exceeds 1.0", pk);
    double e = rmsWin(r, 10.0, 30.0);
    CHECK(e > 0.01 && e < 0.5, "30s: RMS %.4f outside sane range", e);
    std::printf("testThirtySeconds done (peak=%.3f rms=%.3f)\n", pk, e);
}

// ---------------------------------------------------------------------------
// 11. Voice switching + tap-to-stop + convolution tail decay.
// ---------------------------------------------------------------------------
void testSwitching() {
    sg9::Sg9Params p; // defaults (space on -> tail measurable)
    sg9::Sg9Dsp d = makeDsp(p);
    d.triggerVoice(5);
    Render r1 = render(d, 2.0);
    CHECK(finiteBuf(r1), "switch: NaN/Inf in first voice");
    d.triggerVoice(2); // switch while sounding
    CHECK(d.getActiveTrigger() == 2, "switch: active trigger != 2");
    Render r2 = render(d, 2.0);
    CHECK(finiteBuf(r2), "switch: NaN/Inf after voice switch");
    d.triggerVoice(2); // tap active voice -> stop all
    Render r3 = render(d, 3.0);
    CHECK(finiteBuf(r3), "switch: NaN/Inf during release");
    CHECK(d.getActiveTrigger() == -1, "stop: voice still active after release");
    // Tail: render 40 s more; early tail must be non-silent, late tail drained.
    Render r4 = render(d, 40.0);
    CHECK(finiteBuf(r4), "switch: NaN/Inf in tail");
    double early = rmsWin(r4, 3.0, 6.0);
    double late = rmsWin(r4, 37.0, 40.0);
    std::printf("  tail: early=%.4f late=%.5f\n", early, late);
    CHECK(early > 0.005, "tail: early reverb not audible (%.4f)", early);
    CHECK(late < 0.03, "tail: late reverb did not decay (%.4f)", late);
    std::printf("testSwitching done\n");
}

// ---------------------------------------------------------------------------
// 12. Dropout regression (Issue 1): render 10 s; after the 5 s attack, no
// 100 ms window may drop below 5% of the max window RMS (catches dropouts
// to silence; musical dynamics bottom out around 12%).
// ---------------------------------------------------------------------------
void testNoDropouts() {
    sg9::Sg9Params p; // defaults
    sg9::Sg9Dsp d = makeDsp(p);
    d.triggerVoice(5);
    Render r = render(d, 10.0);
    CHECK(finiteBuf(r), "dropout: NaN/Inf");
    const int W = (int)(0.1 * kSr); // 100 ms
    const int nw = r.n / W;
    double mx = 0.0;
    std::vector<double> wrms((size_t)nw, 0.0);
    for (int w = 0; w < nw; ++w) {
        double s = 0.0;
        for (int i = 0; i < W; ++i) {
            int idx = w * W + i;
            s += 0.5 * ((double)r.L[idx] * r.L[idx] + (double)r.R[idx] * r.R[idx]);
        }
        wrms[(size_t)w] = std::sqrt(s / W);
        mx = std::max(mx, wrms[(size_t)w]);
    }
    for (int w = 0; w < nw; ++w) {
        double t = w * 0.1;
        if (t < 5.0) continue; // attack
        CHECK(wrms[(size_t)w] > 0.05 * mx,
              "dropout: window at %.1fs RMS %.4f < 5%% of max %.4f",
              t, wrms[(size_t)w], mx);
    }
    std::printf("testNoDropouts done (maxWin=%.4f)\n", mx);
}

} // namespace

int main() {
    std::printf("SG-9 web-engine DSP tests @ %.0f Hz\n", kSr);
    testChakras();
    testDronePreGain();
    testPulseGrace();
    testPadVoicings();
    testBeatPatterns();
    testTempoCombos();
    testDroneTones();
    testHarmonyFollow();
    testDeterminism();
    testThirtySeconds();
    testSwitching();
    testNoDropouts();
    std::printf("--------------------------------------------------\n");
    if (gFails == 0)
        std::printf("ALL TESTS PASSED (%d checks)\n", gChecks);
    else
        std::printf("%d FAILURES out of %d checks\n", gFails, gChecks);
    return gFails == 0 ? 0 : 1;
}
