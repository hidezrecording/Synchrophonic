#pragma once
#include "Sg9Processor.h"
#include <cmath>
#include <cstdint>

// Synchrophonic editor — the web-app mockup as a native GUI.
//
// Layout (default 880x720, resizable):
//   * The dark chakra-figure photo (embedded via the Sg9Assets binary-data
//     target) is the full-bleed background — the interface IS the figure.
//   * Nine chakra hit zones sit on the figure's symbols: tap to
//     trigger/stop a voice (monophonic), drag vertically to set its level;
//     the sounding chakra glows with its live level.
//   * Two VU meters (scale arc + needle only, no bezel) flank the head.
//   * Bottom strip: five vertical faders in a row (DRONE PULSE PADS BEAT
//     SPACE) with value boxes, and directly beneath each fader its compact
//     selector dropdown (drone tone, tempo source, pad voicing, beat
//     speed, beat pattern). A Pulse toggle and a small voice/grace readout
//     sit in the right-hand utility column.
//   * Animated incense smoke rises from the sticks in the figure's hands
//     (IncenseOverlay, transparent + mouse-transparent, ~30 fps).
//
// Zone coordinates are photo-normalized (0..1) against the figure image
// (aspect 1392:1680). Index order = kChakras order (0 Crown .. 8 Foundation).

// ---------------------------------------------------------------------------
// Sg9VuMeter — floating VU meter: JUST the scale arc + needle against the
// dark background. No bezel, no box, no glass, no screws, no face.
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
// IncenseOverlay — animated incense smoke from the sticks in the figure's
// hands. Transparent and mouse-transparent; paints above the figure (and the
// zone glow) but below the faders/controls. Driven by its own ~30 Hz timer.
// Geometry is photo-normalized (web SVG space, 1170x1413).
// ---------------------------------------------------------------------------
class IncenseOverlay : public juce::Component, private juce::Timer {
public:
    IncenseOverlay();
    void setPhotoRect(float x, float y, float w, float h);
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    juce::Point<float> toScreen(float nx, float ny) const;

    double t_ = 0.0; // seconds since construction
    float photoX_ = 0.0f, photoY_ = 0.0f, photoW_ = 1.0f, photoH_ = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IncenseOverlay)
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

    // Choice-param index helper (0..numChoices-1), clamped.
    int choiceIndex(const char* paramId, int numChoices) const;
    // True while the pulse-tom grace period is still running after a trigger
    // (3 full pattern cycles; derived from the APVTS tempo/pattern params).
    bool inGracePeriod() const;

    juce::Point<float> photoToScreen(float nx, float ny) const;

    // Voice zone hit-testing in window coords; -1 = no zone.
    int zoneAt(float x, float y) const;

    static juce::Font uiFont(float h) { return juce::Font(juce::FontOptions(h)); }

    Sg9Processor& proc_;
    juce::Image photo_;
    Sg9VuMeter meterL_, meterR_;
    IncenseOverlay incense_;

    // Mixer faders + the selector dropdown beneath each fader, all wired to
    // the APVTS via attachments. Order: DRONE PULSE PADS BEAT SPACE.
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

    // Grace-period tracking (message thread): the DSP restarts its 3-cycle
    // tom grace on every chakra trigger and every pad-voicing change.
    uint32_t lastTriggerMs_ = 0;
    int lastPadVoicing_ = -1;

    // Layout metrics (computed in resized()).
    float photoX_ = 0.0f, photoY_ = 0.0f, photoW_ = 0.0f, photoH_ = 0.0f;
    float hitR_ = 42.0f;
    float stripTop_ = 0.0f;
    juce::Rectangle<float> readoutRect_;
    juce::Rectangle<float> faderLabelRects_[5];
    juce::Rectangle<float> selLabelRects_[5];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sg9Editor)
};
