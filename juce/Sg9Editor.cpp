#include "Sg9Editor.h"
// The chakra-figure photo is embedded by the CMake build:
//
//   juce_add_binary_data(Sg9Assets NAMESPACE Sg9Assets SOURCES
//       assets/chakra-figure.jpg)
//
// (juce_add_binary_data always generates BinaryData.h; the NAMESPACE option
// makes the symbols Sg9Assets::chakra_figure_jpg / Sg9Assets::chakra_figure_jpgSize.)
#include "BinaryData.h"

namespace {

// Photo-normalized (0..1) hit-zone centers against the 1170x1413 source
// image. Index order = kChakras order: 0 Crown (963) .. 8 Foundation (174).
// Positions follow the web app's figure layout.
constexpr float kZoneNX[9] = {
    0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.390f, 0.610f
};
constexpr float kZoneNY[9] = {
    0.122f, 0.218f, 0.351f, 0.470f, 0.590f, 0.725f, 0.868f, 0.956f, 0.956f
};

// Drawn-photo-space hit radius (photo drawn at height 780).
constexpr float kHitRadius = 42.0f;
constexpr float kGlowRadius = 48.0f;

juce::Font sg9Font(float h) { return juce::Font(juce::FontOptions(h)); }

juce::String voiceLevelId(int v) {
    return "v" + juce::String(v + 1) + "_level";
}

const char* kFaderIds[5] = {
    "fader_drone", "fader_pulse", "fader_pads", "fader_beat", "fader_space"
};
const char* kFaderNames[5] = { "DRONE", "PULSE", "PADS", "BEAT", "SPACE" };

const char* kSelIds[5] = {
    "drone_tone", "tempo_source", "pad_voicing", "beat_speed", "drum_pattern"
};
const char* kSelNames[5] = {
    "DRONE TONE", "TEMPO SOURCE", "PAD VOICING", "BEAT SPEED", "BEAT PATTERN"
};
const char* kSelItems[5][9] = {
    { "Sa-Pa-Sa", "Sa", "Sa-Sa", "Sa-Ma-Sa", "Pa-Sa-Sa" },
    { "Sun", "Earth OM", "Earth Day", "Moon", "Schumann" },
    { "Octaves", "Tanpura stack", "Fourth pillars", "Septimal depth",
      "Shruti shimmer", "Minor third veil", "Major thirds" },
    { "Full", "Half", "Quarter" },
    { "Baladi", "Maqsum", "Malfuf", "Keherwa", "Dadra",
      "Rupak", "Jhaptal", "Ektaal", "Teental" },
};
constexpr int kSelCounts[5] = { 5, 5, 7, 3, 9 };

} // namespace

juce::Colour Sg9Editor::zoneColour(int i) {
    switch (juce::jlimit(0, 8, i)) {
        case 0: return juce::Colour(0xffab47bc); // crown (violet)
        case 1: return juce::Colour(0xff3f51b5); // third eye (indigo)
        case 2: return juce::Colour(0xff4fc3f7); // throat (light blue)
        case 3: return juce::Colour(0xff43a047); // heart (green)
        case 4: return juce::Colour(0xfffdd835); // solar plexus (yellow)
        case 5: return juce::Colour(0xfffb8c00); // sacral (orange)
        case 6: return juce::Colour(0xffe53935); // root (red)
        case 7: return juce::Colour(0xffa8793f); // renewal (earth)
        default: return juce::Colour(0xff6f5136); // foundation (earth brown)
    }
}

// ---------------------------------------------------------------------------
// Sg9VuMeter
// ---------------------------------------------------------------------------

Sg9VuMeter::Sg9VuMeter(Sg9Processor& proc, bool leftChannel)
    : proc_(proc), left_(leftChannel) {
    setSize(210, 150);
    startTimerHz(30);
}

void Sg9VuMeter::timerCallback() {
    const float target = left_ ? proc_.getOutMeterL() : proc_.getOutMeterR();
    if (target > smoothed_) smoothed_ += (target - smoothed_) * 0.2f;
    else smoothed_ += (target - smoothed_) * 0.05f;
    repaint();
}

void Sg9VuMeter::paint(juce::Graphics& g) {
    const float w = (float) getWidth(), h = (float) getHeight();
    const float cx = w * 0.5f;
    const float cy = h - 4.0f;
    const float r = juce::jmin(h - 40.0f, (w * 0.5f - 30.0f) / 0.76604444f);

    const float pi = 3.14159265f;
    const float sweep = 100.0f * pi / 180.0f;
    const float startAngle = -pi * 0.5f - sweep * 0.5f;
    const float endAngle   = -pi * 0.5f + sweep * 0.5f;
    const float totalAngle = endAngle - startAngle;

    struct Tick { float db; float pos; const char* label; };
    const Tick ticks[] = {
        {-20.0f, 0.000f, "-20"}, {-10.0f, 0.300f, "-10"},
        { -7.0f, 0.432f,  "-7"}, { -5.0f, 0.520f,  "-5"},
        { -3.0f, 0.610f,  "-3"}, { -2.0f, 0.647f,  "-2"},
        { -1.0f, 0.683f,  "-1"}, {  0.0f, 0.720f,   "0"},
        { +1.0f, 0.813f,  "+1"}, { +2.0f, 0.907f,  "+2"},
        { +3.0f, 1.000f,  "+3"},
    };
    constexpr float kZeroPos = 0.72f;
    const float zeroAngle = startAngle + kZeroPos * totalAngle;

    const juce::Colour warmWhite(0xfff3e2b8);
    const juce::Colour hotZone(0xffff5a1f);
    const juce::Colour needleCol(0xfffaf2e8);

    auto angleFor = [&](float pos) { return startAngle + pos * totalAngle; };

    {
        juce::Path p;
        p.addArc(cx - r, cy - r, r * 2, r * 2, startAngle, zeroAngle, true);
        g.setColour(warmWhite.withAlpha(0.85f));
        g.strokePath(p, juce::PathStrokeType(2.5f));
    }
    {
        juce::Path p;
        p.addArc(cx - r, cy - r, r * 2, r * 2, zeroAngle, endAngle, true);
        g.setColour(hotZone.withAlpha(0.95f));
        g.strokePath(p, juce::PathStrokeType(3.0f));
    }

    g.setFont(sg9Font(12.0f));
    for (const auto& t : ticks) {
        const float a = angleFor(t.pos);
        const float ca = std::cos(a), sa = std::sin(a);
        const bool hot = t.pos >= kZeroPos - 1e-6f;
        const juce::Colour c = hot ? hotZone : warmWhite;
        g.setColour(c.withAlpha(0.9f));
        g.drawLine(cx + ca * r, cy + sa * r,
                   cx + ca * (r - 9.0f), cy + sa * (r - 9.0f), 2.0f);
        const float nr = r + 17.0f;
        g.setColour(c);
        g.drawText(t.label, int(cx + ca * nr) - 24, int(cy + sa * nr) - 10,
                   48, 20, juce::Justification::centred);
    }

    const float targetPos = juce::jlimit(-0.05f, 1.05f, smoothed_ * 0.8f);
    const float na = angleFor(targetPos);
    const float nca = std::cos(na), nsa = std::sin(na);
    g.setColour(needleCol);
    g.drawLine(cx, cy, cx + nca * (r - 6.0f), cy + nsa * (r - 6.0f), 2.0f);

    g.setColour(needleCol);
    g.fillEllipse(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
    g.setColour(juce::Colour(0x88000000));
    g.drawEllipse(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f, 1.0f);

    g.setFont(sg9Font(11.0f));
    g.setColour(juce::Colour(0x66ffffff));
    g.drawText(left_ ? "L" : "R", int(cx) + 10, int(cy) - 24, 24, 16,
               juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
// Sg9Editor
// ---------------------------------------------------------------------------

Sg9Editor::Sg9Editor(Sg9Processor& proc)
    : juce::AudioProcessorEditor(proc), proc_(proc),
      meterL_(proc, true), meterR_(proc, false),
      pulseButton_("Pulse") {
    setSize(1120, 800);

    photo_ = juce::ImageFileFormat::loadFrom(Sg9Assets::chakra_figure_jpg,
                                             (size_t) Sg9Assets::chakra_figure_jpgSize);

    addAndMakeVisible(meterL_);
    addAndMakeVisible(meterR_);

    for (int i = 0; i < 5; ++i) {
        faderSliders_[i].setSliderStyle(juce::Slider::LinearVertical);
        faderSliders_[i].setTextBoxStyle(juce::Slider::TextBoxBelow,
                                         false, 42, 18);
        addAndMakeVisible(faderSliders_[i]);
        faderAttach_[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc_.apvts, kFaderIds[i], faderSliders_[i]);

        juce::StringArray items;
        for (int k = 0; k < kSelCounts[i]; ++k)
            items.add(kSelItems[i][k]);
        selBoxes_[i].addItemList(items, 1);
        addAndMakeVisible(selBoxes_[i]);
        selAttach_[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            proc_.apvts, kSelIds[i], selBoxes_[i]);
    }

    pulseButton_.setClickingTogglesState(true);
    addAndMakeVisible(pulseButton_);
    pulseAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc_.apvts, "pulse_on", pulseButton_);

    startTimerHz(30);
}

Sg9Editor::~Sg9Editor() = default;

void Sg9Editor::layoutPhoto() {
    // 1170x1413 source, aspect 0.828; drawn at height 780, centered.
    photoH_ = 780.0f;
    photoW_ = photoH_ * (1170.0f / 1413.0f);
    photoX_ = (1120.0f - photoW_) * 0.5f;
    photoY_ = 10.0f;
}

juce::Point<float> Sg9Editor::photoToScreen(float nx, float ny) const {
    return {photoX_ + nx * photoW_, photoY_ + ny * photoH_};
}

int Sg9Editor::zoneAt(float x, float y) const {
    for (int i = 0; i < 9; ++i) {
        const auto c = photoToScreen(kZoneNX[i], kZoneNY[i]);
        const float dx = x - c.x, dy = y - c.y;
        if (dx * dx + dy * dy <= kHitRadius * kHitRadius)
            return i;
    }
    return -1;
}

void Sg9Editor::resized() {
    layoutPhoto();

    // Floating VU meters flanking the head, clear of the photo
    // (photo spans x 237..883): 210x150 each.
    meterL_.setBounds(13, 75, 210, 150);
    meterR_.setBounds(897, 75, 210, 150);

    // Five faders in the left margin: vertical sliders with value boxes.
    for (int i = 0; i < 5; ++i) {
        const int x = 16 + i * 41;
        faderSliders_[i].setBounds(x, 268, 38, 400);
        faderLabelRects_[i] = {float(x) - 4.0f, 244.0f, 46.0f, 20.0f};
    }

    // Five selectors in the right margin: label + combo box each.
    for (int i = 0; i < 5; ++i) {
        const int y = 250 + i * 48;
        selBoxes_[i].setBounds(902, y + 20, 200, 26);
        selLabelRects_[i] = {902.0f, float(y), 200.0f, 18.0f};
    }
    pulseButton_.setBounds(902, 510, 200, 36);

    // Selected-voice readout in the bottom-right corner (display only).
    voiceReadout_ = {897.0f, 700.0f, 200.0f, 60.0f};
}

void Sg9Editor::timerCallback() {
    repaint(); // zone glows and readout follow live state
}

void Sg9Editor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    // The photo IS the interface.
    const juce::Rectangle<float> photoRect(photoX_, photoY_, photoW_, photoH_);
    if (photo_.isValid())
        g.drawImage(photo_, photoRect);
    else {
        g.setColour(juce::Colour(0xff141414));
        g.fillRect(photoRect);
        g.setFont(uiFont(24.0f));
        g.setColour(juce::Colour(0x66ffffff));
        g.drawText("Synchrophonic", photoRect, juce::Justification::centred);
    }

    // Per-zone glow: radial gradient in the chakra color, alpha from the
    // live voice meter.
    for (int i = 0; i < 9; ++i) {
        const auto c = photoToScreen(kZoneNX[i], kZoneNY[i]);
        const float level = juce::jlimit(0.0f, 1.0f, proc_.getVoiceLevelMeter(i));
        const float alpha = 0.10f + 0.60f * level;
        const juce::Colour col = zoneColour(i).withAlpha(alpha);
        juce::ColourGradient glow(col, c.x, c.y,
                                  juce::Colour(0x00000000), c.x + kGlowRadius, c.y, true);
        g.setGradientFill(glow);
        g.fillEllipse(c.x - kGlowRadius, c.y - kGlowRadius,
                      kGlowRadius * 2, kGlowRadius * 2);

        // Small marker dot at the zone center (discoverability).
        g.setColour(zoneColour(i).withAlpha(0.55f + 0.35f * level));
        const float dr = (i >= 7) ? 7.0f : 5.0f; // foundation dots are UI-drawn
        g.fillEllipse(c.x - dr, c.y - dr, dr * 2, dr * 2);
    }

    // Fader + selector labels in the margins.
    g.setFont(uiFont(11.0f));
    g.setColour(juce::Colour(0x99ffffff));
    for (int i = 0; i < 5; ++i) {
        g.drawText(kFaderNames[i], faderLabelRects_[i],
                   juce::Justification::centred);
        g.drawText(kSelNames[i], selLabelRects_[i],
                   juce::Justification::centredLeft);
    }

    // Selected-voice readout (bottom-right corner, display only).
    {
        g.setColour(juce::Colour(0xa6000000));
        g.fillRoundedRectangle(voiceReadout_, 9.0f);
        g.setColour(juce::Colour(0x40ffffff));
        g.drawRoundedRectangle(voiceReadout_, 9.0f, 1.0f);

        g.setFont(uiFont(15.0f));
        g.setColour(juce::Colour(0xffffffff));
        g.drawText(juce::String(Sg9Processor::voiceName(selectedVoice_)) + " - " +
                   juce::String(int(Sg9Processor::voiceFreq(selectedVoice_))) + " Hz",
                   voiceReadout_.getX(), voiceReadout_.getY() + 6.0f,
                   voiceReadout_.getWidth(), 24.0f,
                   juce::Justification::centred);

        float lvl = 58.0f;
        if (auto* p = proc_.apvts.getParameter(voiceLevelId(selectedVoice_)))
            lvl = p->convertFrom0to1(p->getValue()); // 0..100 natural range
        g.setFont(uiFont(12.0f));
        g.setColour(zoneColour(selectedVoice_));
        g.drawText("Level " + juce::String(int(lvl + 0.5f)) + "%",
                   voiceReadout_.getX(), voiceReadout_.getY() + 32.0f,
                   voiceReadout_.getWidth(), 20.0f,
                   juce::Justification::centred);
    }
}

void Sg9Editor::mouseDown(const juce::MouseEvent& e) {
    const float x = e.position.x, y = e.position.y;

    const int z = zoneAt(x, y);
    if (z >= 0) {
        selectedVoice_ = z;
        dragVoice_ = z;
        dragStartY_ = y;
        dragStartLevel_ = 58.0f;
        if (auto* p = proc_.apvts.getParameter(voiceLevelId(z)))
            dragStartLevel_ = p->convertFrom0to1(p->getValue()); // 0..100
        // Chakra trigger (monophonic touch instrument): tap the sounding
        // voice to stop all sound, otherwise trigger the new voice.
        if (proc_.getActiveTrigger() == z) proc_.requestRelease(z);
        else proc_.requestTrigger(z);
        repaint();
    }
}

void Sg9Editor::mouseDrag(const juce::MouseEvent& e) {
    if (dragVoice_ < 0)
        return;
    const float v = juce::jlimit(0.0f, 100.0f,
                                 dragStartLevel_ - (e.position.y - dragStartY_) / 150.0f * 100.0f);
    if (auto* p = proc_.apvts.getParameter(voiceLevelId(dragVoice_)))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}
