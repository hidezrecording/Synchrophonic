// Headless test of the REAL Sg9Processor plugin path.
//
// Compiles Sg9Processor.cpp with SG9_HEADLESS (no GUI modules in the test
// binary's editor) and drives processBlock() exactly like a DAW would:
// parameter mapping, MIDI (note on/off, CC1), presets, meters, silence
// toggle, state round-trip.
//
// Build (one line):
//   g++ -std=c++17 -O2 -DSG9_HEADLESS -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_USE_CURL=0 -I<juce>/modules -Ijuce -I. tests/test_sg9_processor_headless.cpp juce/Sg9Processor.cpp dsp/Sg9Dsp.cpp build/juce_headless/*.o -o build/test_sg9_processor_headless -lpthread -ldl

#include "../juce/Sg9Processor.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

bool finiteBuf(const juce::AudioBuffer<float>& b) {
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = 0; n < b.getNumSamples(); ++n)
            if (!std::isfinite(d[n])) return false;
    }
    return true;
}

float peakBuf(const juce::AudioBuffer<float>& b) {
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::fabs(d[n]));
    }
    return p;
}

float rmsBuf(const juce::AudioBuffer<float>& b) {
    double s = 0;
    long n2 = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = 0; n < b.getNumSamples(); ++n) { s += d[n] * d[n]; ++n2; }
    }
    return float(std::sqrt(s / n2));
}

// RMS of the last `tailSeconds` of a rendered buffer: lets the reverb/delay
// tails and the master ramp settle before measuring silence.
float rmsTail(const juce::AudioBuffer<float>& b, double tailSeconds,
              double sr = 44100.0) {
    const int total = b.getNumSamples();
    const int start = std::max(0, total - int(tailSeconds * sr));
    double s = 0;
    long n2 = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = start; n < total; ++n) { s += d[n] * d[n]; ++n2; }
    }
    return float(std::sqrt(s / n2));
}

void setParam(Sg9Processor& proc, const char* id, float v01) {
    if (auto* p = proc.apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(v01);
        p->endChangeGesture();
    }
}

void setParamNatural(Sg9Processor& proc, const char* id, float natural) {
    if (auto* p = proc.apvts.getParameter(id))
        setParam(proc, id, p->convertTo0to1(natural));
}

// Render `seconds` of the synth through processBlock in host-sized blocks.
// `midi` is consumed on the first block only (like a DAW delivering events).
juce::AudioBuffer<float> render(Sg9Processor& proc, double seconds,
                                juce::MidiBuffer midi = juce::MidiBuffer(),
                                int block = 512, double sr = 44100.0) {
    const int total = int(seconds * sr);
    juce::AudioBuffer<float> out(2, total);
    int done = 0;
    while (done < total) {
        const int n = std::min(block, total - done);
        juce::AudioBuffer<float> blk(2, n);
        proc.processBlock(blk, midi);
        midi.clear(); // events delivered on the first block
        for (int c = 0; c < 2; ++c)
            out.copyFrom(c, done, blk.getReadPointer(c), n);
        done += n;
    }
    return out;
}

void allVoices(Sg9Processor& proc, float v) {
    for (int i = 1; i <= 9; ++i)
        setParamNatural(proc, ("v" + juce::String(i) + "_level").toRawUTF8(), v);
}

} // namespace

int main() {
    Sg9Processor proc;
    proc.prepareToPlay(44100.0, 512);

    check(proc.getLatencySamples() == 0, "host latency == 0 samples");

    // 1. Default render: finite, bounded, meters alive (trigger Solar Plexus).
    {
        proc.requestTrigger(4);
        auto out = render(proc, 2.0);
        check(finiteBuf(out), "default render: no NaN/Inf");
        const float pk = peakBuf(out);
        std::printf("  (default peak %.4f)\n", pk);
        check(pk <= 1.0f, "default render: bounded");
        check(proc.getOutMeterL() > 0.001f, "output L meter alive");
        check(proc.getOutMeterR() > 0.001f, "output R meter alive");
        float vsum = 0;
        for (int i = 0; i < 9; ++i) vsum += proc.getVoiceLevelMeter(i);
        std::printf("  (outL %.3f, outR %.3f, voice sum %.3f)\n",
                    proc.getOutMeterL(), proc.getOutMeterR(), vsum);
        check(vsum > 0.01f, "voice level meters alive");
    }

    // 2. MIDI: noteOn / noteOff / CC1 (mod wheel) must not break the render.
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 100), 100);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 200);
        auto out = render(proc, 1.0, midi);
        check(finiteBuf(out), "MIDI noteOn/noteOff/CC1: finite");
    }

    // 3. Factory preset through the host parameter path.
    {
        proc.applyParamsToHost(Sg9Processor::presetFullAscension());
        auto* v1 = proc.apvts.getParameter("v1_level");
        auto* ascRun = proc.apvts.getParameter("asc_run");
        auto* ascTime = proc.apvts.getParameter("asc_time");
        check(v1 && std::fabs(v1->getValue() - 0.4f) < 1e-6f,
              "presetFullAscension: v1_level == 0.4");
        check(ascRun && ascRun->getValue() == 1.0f,
              "presetFullAscension: asc_run on");
        check(ascTime && std::fabs(ascTime->convertFrom0to1(ascTime->getValue()) - 30.0f) < 1e-3f,
              "presetFullAscension: asc_time == 30 min");
        proc.applyParamsToHost(Sg9Processor::presetRoot());
        auto* v3l = proc.apvts.getParameter("v3_level");
        auto* v3b = proc.apvts.getParameter("v3_body");
        auto* v1l = proc.apvts.getParameter("v1_level");
        check(v3l && std::fabs(v3l->getValue() - 0.8f) < 1e-6f,
              "presetRoot: v3_level == 0.8");
        check(v3b && v3b->getValue() == 1.0f, "presetRoot: v3_body on");
        check(v1l && v1l->getValue() == 0.0f, "presetRoot: other voices at 0");
        proc.applyParamsToHost(Sg9Processor::presetFullSpectrum());
    }

    // 4. Master param drives output level through the APVTS path.
    {
        proc.requestTrigger(4);
        render(proc, 2.5); // let the ~2 s trigger attack settle
        setParamNatural(proc, "master", 0.9f);
        auto loud = render(proc, 0.5);
        setParamNatural(proc, "master", 0.1f);
        auto quiet = render(proc, 0.5);
        const float rl = rmsBuf(loud), rq = rmsBuf(quiet);
        std::printf("  (master 0.9 RMS %.4f vs 0.1 RMS %.4f)\n", rl, rq);
        check(rl > rq * 2.0f, "master param drives output via APVTS");
        setParamNatural(proc, "master", 0.8f);
    }

    // 5. Voice level params: fader to zero on the sounding voice gives the
    // 3 s trigger release; then near silence once the FX tails settle.
    // The 2000 ms space-echo tap recirculates at 0.35 feedback, so its tail
    // runs ~8 s; the reverb rings ~1 s.
    {
        proc.requestTrigger(4);
        render(proc, 1.0); // voice sounding
        allVoices(proc, 0.0f); // fader to zero -> 3 s release
        setParamNatural(proc, "air", 0.0f); // Air is an independent bed
        setParam(proc, "cosmos", 0.0f); // COSMOS is an independent bed
        auto out = render(proc, 10.0);
        const float r = rmsTail(out, 0.5);
        std::printf("  (all voices 0, tail RMS %.2e)\n", r);
        check(r < 1e-3f, "voice levels at 0: near silence");
        allVoices(proc, 0.35f);
        setParamNatural(proc, "air", 0.15f);
        setParam(proc, "cosmos", 1.0f);
    }

    // 6. State round-trip preserves parameters.
    {
        setParamNatural(proc, "v5_level", 0.77f);
        setParamNatural(proc, "asc_time", 20.0f);
        setParam(proc, "asc_run", 1.0f);
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        Sg9Processor proc2;
        proc2.setStateInformation(mb.getData(), (int) mb.getSize());
        auto* a = proc.apvts.getParameter("v5_level");
        auto* b = proc2.apvts.getParameter("v5_level");
        auto* c = proc.apvts.getParameter("asc_time");
        auto* d = proc2.apvts.getParameter("asc_time");
        auto* e = proc.apvts.getParameter("asc_run");
        auto* f = proc2.apvts.getParameter("asc_run");
        check(a && b && std::fabs(a->getValue() - b->getValue()) < 1e-6f,
              "state round-trip: v5_level");
        check(c && d && std::fabs(c->getValue() - d->getValue()) < 1e-6f,
              "state round-trip: asc_time");
        check(e && f && e->getValue() == f->getValue(),
              "state round-trip: asc_run");
        setParamNatural(proc, "v5_level", 0.35f);
        setParamNatural(proc, "asc_time", 10.0f);
        setParam(proc, "asc_run", 0.0f);
    }

    // 7. Sound off -> near silence once the ~100 ms master ramp and the
    // FX tails settle; on again -> audible (the trigger is still held).
    {
        proc.requestTrigger(4);
        render(proc, 2.5); // attack settles
        setParam(proc, "sound", 0.0f);
        auto silent = render(proc, 2.0);
        const float r = rmsTail(silent, 0.5);
        std::printf("  (sound off tail RMS %.2e)\n", r);
        check(r < 1e-3f, "sound=false: near silence");
        check(proc.getOutMeterL() < 1e-3f, "sound=false: output meters fall");
        setParam(proc, "sound", 1.0f);
        auto audible = render(proc, 0.5);
        check(rmsBuf(audible) > 0.001f, "sound=true: audible again");
    }

    // 8. Odd block sizes (host may use any).
    {
        auto out = render(proc, 0.5, juce::MidiBuffer(), 137);
        check(finiteBuf(out), "137-sample blocks: finite");
    }

    // 9. Chakra trigger queue (editor -> DSP, lock-free): monophonic
    // steal + release + out-of-range guard.
    {
        proc.requestTrigger(2);
        render(proc, 0.5);
        check(proc.getActiveTrigger() == 2, "requestTrigger: active voice == 2");
        proc.requestTrigger(5);
        render(proc, 0.5);
        check(proc.getActiveTrigger() == 5, "requestTrigger: steal -> active == 5");
        proc.requestRelease(5);
        render(proc, 0.5);
        check(proc.getActiveTrigger() == -1, "requestRelease: active cleared");
        proc.requestTrigger(9); // out of range: ignored
        render(proc, 0.2);
        check(proc.getActiveTrigger() == -1, "requestTrigger: out-of-range ignored");
    }

    // 10. COSMOS param path: the bed is audible with cosmos on (no chakra
    // triggered, air off) and silent with it off.
    {
        setParamNatural(proc, "air", 0.0f);
        allVoices(proc, 0.0f);
        setParam(proc, "cosmos", 1.0f);
        setParamNatural(proc, "cosmos_level", 0.7f);
        auto on = render(proc, 3.0);
        const float rOn = rmsTail(on, 1.0);
        setParam(proc, "cosmos", 0.0f);
        // Long render: the 2000 ms space-echo tap recirculates at 0.35
        // feedback, so its tail runs ~8 s (same as test 5).
        auto off = render(proc, 12.0);
        const float rOff = rmsTail(off, 0.5);
        std::printf("  (cosmos on RMS %.4f vs off %.2e)\n", rOn, rOff);
        check(rOn > 0.005f, "cosmos=true: bed audible");
        check(rOff < 1e-4f, "cosmos=false: bed silent");
        setParam(proc, "cosmos", 1.0f);
        allVoices(proc, 0.35f);
        setParamNatural(proc, "air", 0.15f);
    }

    // 11. Planetary-layer params: musical defaults + state round-trip.
    {
        auto* cosmos = proc.apvts.getParameter("cosmos");
        auto* cLvl = proc.apvts.getParameter("cosmos_level");
        auto* rpan = proc.apvts.getParameter("respan");
        auto* rdep = proc.apvts.getParameter("respan_depth");
        auto* sess = proc.apvts.getParameter("session");
        check(cosmos && cosmos->getValue() == 1.0f, "default: cosmos on");
        check(cLvl && std::fabs(cLvl->convertFrom0to1(cLvl->getValue()) - 0.7f) < 1e-6f,
              "default: cosmos_level 0.7");
        check(rpan && rpan->getValue() == 1.0f, "default: respan on");
        check(rdep && std::fabs(rdep->convertFrom0to1(rdep->getValue()) - 0.15f) < 1e-6f,
              "default: respan_depth 0.15");
        check(sess && sess->getValue() == 1.0f, "default: session on");
        setParam(proc, "respan_rate", 1.0f); // choice index 2 -> 20.8 Hz
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        Sg9Processor proc2;
        proc2.setStateInformation(mb.getData(), (int)mb.getSize());
        auto* r2 = proc2.apvts.getParameter("respan_rate");
        check(r2 && r2->getValue() == 1.0f, "state round-trip: respan_rate");
        setParam(proc, "respan_rate", 0.0f);
    }

    // 12. Rhythmic-layer param defaults + APVTS plumbing.
    {
        auto* mantra = proc.apvts.getParameter("pulse");
        auto* mlev = proc.apvts.getParameter("pulse_level");
        auto* mpace = proc.apvts.getParameter("pulse_pace");
        auto* breath = proc.apvts.getParameter("breath");
        auto* bdep = proc.apvts.getParameter("breath_depth");
        check(mantra && mantra->getValue() == 1.0f, "default: pulse on");
        check(mlev && std::fabs(mlev->convertFrom0to1(mlev->getValue()) - 0.6f) < 1e-6f,
              "default: pulse_level 0.6");
        check(mpace && std::fabs(mpace->convertFrom0to1(mpace->getValue()) - 1.0f) < 1e-6f,
              "default: pulse_pace 1.0");
        check(breath && breath->getValue() == 1.0f, "default: breath on");
        check(bdep && std::fabs(bdep->convertFrom0to1(bdep->getValue()) - 0.5f) < 1e-6f,
              "default: breath_depth 0.5");
    }

    // 13. applyParamsToHost carries the rhythmic layer both ways.
    {
        sg9::Sg9Params p;
        p.pulseOn = false; p.pulseLevel = 0.9f; p.pulsePace = 1.2f;
        p.breathOn = false; p.breathDepth = 0.8f;
        proc.applyParamsToHost(p);
        auto* mantra = proc.apvts.getParameter("pulse");
        auto* mlev = proc.apvts.getParameter("pulse_level");
        auto* mpace = proc.apvts.getParameter("pulse_pace");
        auto* breath = proc.apvts.getParameter("breath");
        auto* bdep = proc.apvts.getParameter("breath_depth");
        check(mantra && mantra->getValue() == 0.0f, "preset->host: pulse off");
        check(mlev && std::fabs(mlev->convertFrom0to1(mlev->getValue()) - 0.9f) < 1e-3f,
              "preset->host: pulse_level 0.9");
        check(mpace && std::fabs(mpace->convertFrom0to1(mpace->getValue()) - 1.2f) < 1e-3f,
              "preset->host: pulse_pace 1.2");
        check(breath && breath->getValue() == 0.0f, "preset->host: breath off");
        check(bdep && std::fabs(bdep->convertFrom0to1(bdep->getValue()) - 0.8f) < 1e-3f,
              "preset->host: breath_depth 0.8");
        // And back to defaults for the remaining tests.
        sg9::Sg9Params d;
        proc.applyParamsToHost(d);
    }

    // 14. State round-trip with non-default rhythmic values + a smoke
    // render with the full rhythmic layer on (finite, bounded).
    {
        setParam(proc, "pulse", 0.0f);
        setParamNatural(proc, "pulse_level", 0.9f);
        setParamNatural(proc, "breath_depth", 1.0f);
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        Sg9Processor proc2;
        proc2.setStateInformation(mb.getData(), (int)mb.getSize());
        auto* a = proc.apvts.getParameter("pulse");
        auto* b = proc2.apvts.getParameter("pulse");
        auto* c = proc.apvts.getParameter("pulse_level");
        auto* d = proc2.apvts.getParameter("pulse_level");
        auto* e = proc.apvts.getParameter("breath_depth");
        auto* f = proc2.apvts.getParameter("breath_depth");
        check(a && b && a->getValue() == b->getValue(),
              "state round-trip: mantra");
        check(c && d && std::fabs(c->getValue() - d->getValue()) < 1e-6f,
              "state round-trip: mantra_level");
        check(e && f && std::fabs(e->getValue() - f->getValue()) < 1e-6f,
              "state round-trip: breath_depth");
        setParam(proc, "pulse", 1.0f);
        setParamNatural(proc, "pulse_level", 0.5f);
        setParamNatural(proc, "breath_depth", 0.5f);

        proc.requestTrigger(4);
        auto out = render(proc, 3.0);
        check(finiteBuf(out), "rhythmic layer on: finite");
        const float pk = peakBuf(out);
        std::printf("  (rhythmic layer peak %.4f)\n", pk);
        check(pk <= 0.995f, "rhythmic layer on: peak <= 0.995");
    }

    std::printf(failures == 0 ? "ALL HEADLESS PLUGIN TESTS PASSED\n"
                              : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
