#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Sg9Dsp.h"

// Synchrophonic — JUCE wrapper.
//
// The entire sound lives in ../dsp/Sg9Dsp.{h,cpp} (framework-free C++17,
// web-engine port). This class only bridges DAW parameters/MIDI/audio to
// sg9::Sg9Params and the sg9::Sg9Dsp engine. Parameter IDs below are stable:
// hosts store automation and presets against them, so do not rename once
// released.
//
// Synchrophonic is an INSTRUMENT: no audio input bus, stereo output bus,
// MIDI input. The Standalone target is the same processor as a playable app
// (the "test host"): identical defaults, no DAW required.

class Sg9Processor : public juce::AudioProcessor {
public:
    Sg9Processor();
    ~Sg9Processor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Synchrophonic"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 32.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Push a full Sg9Params set into the hosted parameters (presets, etc.).
    void applyParamsToHost(const sg9::Sg9Params& p);

    // Meter feeds for the editor (0..~1).
    float getVoiceLevelMeter(int i) const;
    float getOutMeterL() const { return outMeterL_.load(); }
    float getOutMeterR() const { return outMeterR_.load(); }

    // Chakra trigger (monophonic touch instrument): the editor calls these
    // on the message thread; the audio thread feeds them to the DSP.
    void requestTrigger(int i) { if (i >= 0 && i < 9) trigReq_.store(i); }
    void requestRelease(int i) { if (i >= 0 && i < 9) relReq_.store(i); }
    int getActiveTrigger() const { return activeTrig_.load(); }

    // Voice identity: index order matches kChakras in Sg9Dsp.h
    // (0 Crown .. 8 Foundation).
    static const char* voiceName(int i);
    static float voiceFreq(int i);

    // Named starting points (voice indices follow kChakras order).
    static sg9::Sg9Params presetCrown();
    static sg9::Sg9Params presetThirdEye();
    static sg9::Sg9Params presetThroat();
    static sg9::Sg9Params presetHeart();
    static sg9::Sg9Params presetSolarPlexus();
    static sg9::Sg9Params presetSacral();
    static sg9::Sg9Params presetRoot();
    static sg9::Sg9Params presetFoundation();
    static sg9::Sg9Params presetFullSpectrum();

private:
    // Parameter pointers cached once in the constructor so the audio thread
    // never touches the APVTS lookup map or allocates strings. Bool params
    // are cached as their raw 0..1 atomics and thresholded at 0.5; choice
    // params are cached as raw 0..N-1 atomics and rounded.
    struct ParamCache {
        std::atomic<float>* voiceLevel[9]{};
        std::atomic<float>* faderDrone{};
        std::atomic<float>* faderPulse{};
        std::atomic<float>* faderPads{};
        std::atomic<float>* faderBeat{};
        std::atomic<float>* faderSpace{};
        std::atomic<float>* droneTone{};
        std::atomic<float>* tempoSource{};
        std::atomic<float>* padVoicing{};
        std::atomic<float>* beatSpeed{};
        std::atomic<float>* beatPattern{};
        std::atomic<float>* pulseOn{};
        std::atomic<float>* masterFader{};
    };
    ParamCache cache_;

    // Read the cached parameters into an Sg9Params struct (audio thread,
    // allocation-free).
    sg9::Sg9Params readParamsFromHost() const;

    sg9::Sg9Dsp dsp_;

    // Lock-free chakra trigger queue (message thread -> audio thread).
    std::atomic<int> trigReq_{-1};
    std::atomic<int> relReq_{-1};
    std::atomic<int> activeTrig_{-1};

    // Meter levels, written on the audio thread, read by the editor.
    std::atomic<float> voiceLevels_[9];
    std::atomic<float> outMeterL_{0.0f};
    std::atomic<float> outMeterR_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sg9Processor)
};
