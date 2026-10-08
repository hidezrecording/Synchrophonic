#include "Sg9Processor.h"
#include <cmath>
// SG9_HEADLESS: compile the DSP + parameter path without any GUI modules,
// so the real processBlock() can be exercised in a console test.
#ifndef SG9_HEADLESS
#include "Sg9Editor.h"
#endif

namespace {

using ParamPtr = std::unique_ptr<juce::RangedAudioParameter>;

// 0..100 voice-level control (matches sg9::Sg9Params::voiceLevels ints).
ParamPtr level100(const juce::String& id, const juce::String& name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, 1}, name,
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), def);
}

// 0..1 fader-style control.
ParamPtr fader01(const juce::String& id, const juce::String& name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, 1}, name,
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def);
}

ParamPtr toggle(const juce::String& id, const juce::String& name, bool def) {
    return std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{id, 1}, name, def);
}

ParamPtr choice(const juce::String& id, const juce::String& name,
                juce::StringArray items, int def) {
    return std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{id, 1}, name, items, def);
}

// Voice-level defaults mirror sg9::Sg9Params (kChakras order 0..8).
constexpr float kVoiceDefaults[9] = { 55, 48, 52, 60, 68, 58, 62, 42, 46 };

} // namespace

const char* Sg9Processor::voiceName(int i) {
    static const char* names[9] = {
        "Crown", "Third Eye", "Throat", "Heart", "Solar Plexus",
        "Sacral", "Root", "Renewal", "Foundation"
    };
    return names[juce::jlimit(0, 8, i)];
}

float Sg9Processor::voiceFreq(int i) {
    static const float freqs[9] = {
        963.0f, 852.0f, 741.0f, 639.0f, 528.0f,
        417.0f, 396.0f, 285.0f, 174.0f
    };
    return freqs[juce::jlimit(0, 8, i)];
}

juce::AudioProcessorValueTreeState::ParameterLayout
Sg9Processor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Voice levels: v1_..v9_ -> chakras 0..8 (Crown .. Foundation).
    for (int v = 0; v < 9; ++v) {
        const juce::String id = "v" + juce::String(v + 1) + "_level";
        layout.add(level100(id, juce::String(voiceName(v)) + " Level",
                            kVoiceDefaults[v]));
    }

    // The five mixer faders (web-app defaults).
    layout.add(fader01("fader_drone", "Drone", sg9::kDefaultFaderDrone));
    layout.add(fader01("fader_pulse", "Pulse", sg9::kDefaultFaderPulse));
    layout.add(fader01("fader_pads",  "Pads",  sg9::kDefaultFaderPads));
    layout.add(fader01("fader_beat",  "Beat",  sg9::kDefaultFaderBeat));
    layout.add(fader01("fader_space", "Space", sg9::kDefaultFaderSpace));

    // Master output trim (Nathan 2026-10-08): 0..1.25, default 1.0 = unity
    // (the web 0.558 level). Allows turning up 25% or down to silence.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"master_fader", 1}, "Master",
        juce::NormalisableRange<float>(0.0f, 1.25f, 0.001f), 1.0f));

    // Selectors (indices match the sg9::k* tables in Sg9Dsp.h).
    layout.add(choice("drone_tone", "Drone Tone",
        juce::StringArray{"Sa-Pa-Sa", "Sa", "Sa-Sa", "Sa-Ma-Sa", "Pa-Sa-Sa"},
        sg9::kDefaultDroneTone));
    layout.add(choice("tempo_source", "Tempo Source",
        juce::StringArray{"Sun", "Earth OM", "Earth Day", "Moon", "Schumann"},
        sg9::kDefaultTempoSource));
    layout.add(choice("pad_voicing", "Pad Voicing",
        juce::StringArray{"Octaves", "Tanpura stack", "Fourth pillars",
                          "Septimal depth", "Shruti shimmer",
                          "Minor third veil", "Major thirds"},
        sg9::kDefaultPadVoicing));
    layout.add(choice("beat_speed", "Beat Speed",
        juce::StringArray{"Full", "Half", "Quarter"},
        sg9::kDefaultBeatSpeed));
    layout.add(choice("drum_pattern", "Beat Pattern",
        juce::StringArray{"Baladi", "Maqsum", "Malfuf", "Keherwa", "Dadra",
                          "Rupak", "Jhaptal", "Ektaal", "Teental"},
        sg9::kDefaultBeatPattern));

    layout.add(toggle("pulse_on", "Pulse", true));

    return layout;
}

Sg9Processor::Sg9Processor()
    : AudioProcessor(juce::AudioProcessor::BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Synchrophonic", createLayout()) {
    // Cache every parameter pointer once; the audio thread must not hit
    // the APVTS lookup map or build strings per block.
    auto raw = [this](const juce::String& id) {
        return apvts.getRawParameterValue(id);
    };
    for (int v = 0; v < 9; ++v) {
        cache_.voiceLevel[v] =
            raw("v" + juce::String(v + 1) + "_level");
        voiceLevels_[v].store(0.0f);
    }
    cache_.faderDrone  = raw("fader_drone");
    cache_.faderPulse  = raw("fader_pulse");
    cache_.faderPads   = raw("fader_pads");
    cache_.faderBeat   = raw("fader_beat");
    cache_.faderSpace  = raw("fader_space");
    cache_.droneTone   = raw("drone_tone");
    cache_.tempoSource = raw("tempo_source");
    cache_.padVoicing  = raw("pad_voicing");
    cache_.beatSpeed   = raw("beat_speed");
    cache_.beatPattern = raw("drum_pattern");
    cache_.pulseOn     = raw("pulse_on");
    cache_.masterFader = raw("master_fader");

    jassert(cache_.voiceLevel[0] != nullptr && cache_.pulseOn != nullptr);
}

void Sg9Processor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    dsp_.prepare(sampleRate);
    dsp_.reset();
    setLatencySamples(dsp_.getLatencySamples()); // partitioned-convolver latency
}

bool Sg9Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Synchrophonic is a synth: no input bus, stereo output bus only.
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

sg9::Sg9Params Sg9Processor::readParamsFromHost() const {
    sg9::Sg9Params p; // struct defaults per Sg9Dsp.h
    for (int v = 0; v < 9; ++v)
        p.voiceLevels[v] =
            juce::jlimit(0, 100, int(std::round(cache_.voiceLevel[v]->load())));
    p.faderDrone = cache_.faderDrone->load();
    p.faderPulse = cache_.faderPulse->load();
    p.faderPads  = cache_.faderPads->load();
    p.faderBeat  = cache_.faderBeat->load();
    p.faderSpace = cache_.faderSpace->load();
    // Choice params cache their raw 0..N-1 index as float.
    p.droneTone   = juce::jlimit(0, 4, int(std::round(cache_.droneTone->load())));
    p.tempoSource = juce::jlimit(0, 4, int(std::round(cache_.tempoSource->load())));
    p.padVoicing  = juce::jlimit(0, 6, int(std::round(cache_.padVoicing->load())));
    p.beatSpeed   = juce::jlimit(0, 2, int(std::round(cache_.beatSpeed->load())));
    p.beatPattern = juce::jlimit(0, 8, int(std::round(cache_.beatPattern->load())));
    p.pulseOn     = cache_.pulseOn->load() > 0.5f;
    p.masterFader = juce::jlimit(0.0f, 1.25f, cache_.masterFader->load());
    return p;
}

void Sg9Processor::applyParamsToHost(const sg9::Sg9Params& p) {
    auto setF = [this](const juce::String& id, float v) {
        if (auto* par = apvts.getParameter(id))
            par->setValueNotifyingHost(par->convertTo0to1(v));
    };
    auto setB = [this](const juce::String& id, bool b) {
        if (auto* par = apvts.getParameter(id))
            par->setValueNotifyingHost(b ? 1.0f : 0.0f);
    };
    auto setC = [this](const juce::String& id, int idx) {
        if (auto* par = apvts.getParameter(id))
            par->setValueNotifyingHost(par->convertTo0to1(float(idx)));
    };
    for (int v = 0; v < 9; ++v)
        setF("v" + juce::String(v + 1) + "_level",
             float(juce::jlimit(0, 100, p.voiceLevels[v])));
    setF("fader_drone", p.faderDrone);
    setF("fader_pulse", p.faderPulse);
    setF("fader_pads",  p.faderPads);
    setF("fader_beat",  p.faderBeat);
    setF("fader_space", p.faderSpace);
    setC("drone_tone",   juce::jlimit(0, 4, p.droneTone));
    setC("tempo_source", juce::jlimit(0, 4, p.tempoSource));
    setC("pad_voicing",  juce::jlimit(0, 6, p.padVoicing));
    setC("beat_speed",   juce::jlimit(0, 2, p.beatSpeed));
    setC("drum_pattern", juce::jlimit(0, 8, p.beatPattern));
    setB("pulse_on", p.pulseOn);
}

float Sg9Processor::getVoiceLevelMeter(int i) const {
    return voiceLevels_[juce::jlimit(0, 8, i)].load();
}

void Sg9Processor::processBlock(juce::AudioBuffer<float>& buffer,
                                juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // MIDI input: notes drive the chakra triggers (nearest chakra).
    for (const juce::MidiMessageMetadata md : midi) {
        const juce::MidiMessage m = md.getMessage();
        if (m.isNoteOn())
            dsp_.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            dsp_.noteOff(m.getNoteNumber());
    }

    // Chakra triggers from the editor (lock-free queue -> DSP).
    if (int tq = trigReq_.exchange(-1); tq >= 0 && tq < 9) dsp_.triggerVoice(tq);
    if (int rq = relReq_.exchange(-1); rq >= 0 && rq < 9) dsp_.releaseVoice(rq);

    dsp_.setParams(readParamsFromHost());

    // Synchrophonic is a synth: ignore any input, generate into the output.
    const int numOut = juce::jmin(buffer.getNumChannels(), 2);
    if (numOut >= 2) {
        dsp_.process(buffer.getWritePointer(0), buffer.getWritePointer(1),
                     numSamples);
    } else {
        // A non-stereo output layout should never reach us (see
        // isBusesLayoutSupported), but never hand the host uninitialised
        // audio: clear instead.
        buffer.clear();
    }
    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear(c, 0, numSamples);

    for (int v = 0; v < 9; ++v)
        voiceLevels_[v].store(dsp_.getVoiceLevel(v));
    outMeterL_.store(dsp_.getOutMeterL());
    outMeterR_.store(dsp_.getOutMeterR());
    activeTrig_.store(dsp_.getActiveTrigger());
}

void Sg9Processor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml())
        AudioProcessor::copyXmlToBinary(*xml, destData);
}

void Sg9Processor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = AudioProcessor::getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* Sg9Processor::createEditor() {
#ifdef SG9_HEADLESS
    return nullptr;
#else
    return new Sg9Editor(*this);
#endif
}

// ---------------------------------------------------------------------------
// Factory presets — named starting points (kChakras order: 0 Crown .. 8
// Foundation). Each starts from the Sg9Params struct defaults and overrides
// the preset-specific fields only.
// ---------------------------------------------------------------------------

namespace {
sg9::Sg9Params soloVoice(int v) {
    sg9::Sg9Params p;
    for (int i = 0; i < 9; ++i)
        p.voiceLevels[i] = 0;
    p.voiceLevels[v] = 80;
    return p;
}
} // namespace

sg9::Sg9Params Sg9Processor::presetCrown()       { return soloVoice(0); }
sg9::Sg9Params Sg9Processor::presetThirdEye()    { return soloVoice(1); }
sg9::Sg9Params Sg9Processor::presetThroat()      { return soloVoice(2); }
sg9::Sg9Params Sg9Processor::presetHeart()       { return soloVoice(3); }
sg9::Sg9Params Sg9Processor::presetSolarPlexus() { return soloVoice(4); }
sg9::Sg9Params Sg9Processor::presetSacral()      { return soloVoice(5); }
sg9::Sg9Params Sg9Processor::presetRoot()        { return soloVoice(6); }

sg9::Sg9Params Sg9Processor::presetFoundation() {
    sg9::Sg9Params p;
    for (int i = 0; i < 9; ++i)
        p.voiceLevels[i] = 0;
    p.voiceLevels[7] = 70;
    p.voiceLevels[8] = 70;
    return p;
}

sg9::Sg9Params Sg9Processor::presetFullSpectrum() {
    sg9::Sg9Params p; // struct defaults
    return p;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Sg9Processor();
}
