#pragma once
#include "Sg9Processor.h"

// Synchrophonic editor (functional placeholder — the native GUI comes later).
//
// The chakra-figure photo (embedded via the Sg9Assets binary-data target)
// is the backdrop on a black background. The nine chakra hit zones sit on
// the figure's chakra symbols (tap to trigger, drag vertically for level);
// per-zone glow follows the live voice meters. Two floating VU meters
// (scale arc + needle only) flank the head at L/R. The five mixer faders
// live in the left margin; the five selectors and the Pulse toggle live in
// the right margin. The selected-voice readout sits bottom-right.
//
// Zone coordinates are photo-normalized (0..1) against the 1170x1413 source
// image. Index order = kChakras order (0 Crown .. 8 Foundation).

// ---------------------------------------------------------------------------
// Sg9VuMeter — floating VU meter: JUST the scale arc + needle against the
// black window background. No bezel, no box, no glass, no screws, no face.
// ---------------------------------------------------------------------------
class Sg9VuMeter : public juce::Component, private juce::Timer {
public:
    explicit Sg9VuMeter(Sg9Processor& proc, bool leftChannel);
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    Sg9Processor& proc_;
    bool left_;
    float smoothed_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sg9VuMeter)
};

// ---------------------------------------------------------------------------
// Sg9Editor
// ---------------------------------------------------------------------------
class Sg9Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Sg9Editor(Sg9Processor& proc);
    ~Sg9Editor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { dragVoice_ = -1; }

    // Chakra colors in kChakras order (0 Crown .. 8 Foundation).
    static juce::Colour zoneColour(int i);

private:
    void timerCallback() override;

    // Photo geometry (computed in resized()).
    void layoutPhoto();
    juce::Point<float> photoToScreen(float nx, float ny) const;

    // Voice zone hit-testing in window coords; -1 = no zone.
    int zoneAt(float x, float y) const;

    static juce::Font uiFont(float h) { return juce::Font(juce::FontOptions(h)); }

    Sg9Processor& proc_;
    juce::Image photo_;
    Sg9VuMeter meterL_, meterR_;

    // Mixer faders (left margin) + selector boxes (right margin), all wired
    // to the APVTS via attachments.
    juce::Slider faderSliders_[5];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttach_[5];
    juce::ComboBox selBoxes_[5];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> selAttach_[5];
    juce::TextButton pulseButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pulseAttach_;

    int selectedVoice_ = 5; // Sacral
    int dragVoice_ = -1;
    float dragStartY_ = 0.0f;
    float dragStartLevel_ = 58.0f;

    float photoX_ = 0.0f, photoY_ = 0.0f, photoW_ = 0.0f, photoH_ = 0.0f;
    juce::Rectangle<float> voiceReadout_; // bottom-right corner, display only
    juce::Rectangle<float> faderLabelRects_[5];
    juce::Rectangle<float> selLabelRects_[5];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sg9Editor)
};
