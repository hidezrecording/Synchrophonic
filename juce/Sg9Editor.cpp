#include "Sg9Editor.h"
// The chakra-figure photo is embedded by the CMake build:
//
//   juce_add_binary_data(Sg9Assets NAMESPACE Sg9Assets SOURCES
//       assets/chakra-figure.png)
//
// (juce_add_binary_data always generates BinaryData.h; the NAMESPACE option
// makes the symbols Sg9Assets::chakrafigure_png / Sg9Assets::chakrafigure_pngSize.)
#include "BinaryData.h"

namespace {

// Photo-normalized (0..1) hit-zone centers. Index order = kChakras order:
// 0 Crown (963) .. 8 Foundation (174). Positions follow the web app layout.
constexpr float kZoneNX[9] = {
    0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.390f, 0.610f
};
constexpr float kZoneNY[9] = {
    0.122f, 0.218f, 0.351f, 0.470f, 0.590f, 0.725f, 0.868f, 0.956f, 0.956f
};
constexpr float kGlowRadius = 48.0f;

// Incense geometry in photo-normalized coords (web SVG space 1170x1413).
// Viewer-left stick: (158,1312)->(122,1138), ember at (122,1138).
// Viewer-right stick: (1012,1312)->(1048,1138), ember at (1048,1138).
struct StickDef { float x0, y0, x1, y1; };
constexpr StickDef kSticks[2] = {
    { 158.0f/1170, 1312.0f/1413,  122.0f/1170, 1138.0f/1413 },
    { 1012.0f/1170, 1312.0f/1413, 1048.0f/1170, 1138.0f/1413 },
};
constexpr float kEmberNY  = 1138.0f/1413;
constexpr float kSmokeY0  = 1104.0f/1413; // puff start (at the ember)
constexpr float kSmokeY1  =  626.0f/1413; // puff end (fully risen)
constexpr float kHazeNY   =  922.0f/1413;

// Pulse grace derivation (matches dsp/Sg9Dsp.h tables).
constexpr float kTempoBpm[5]   = { 118.3f, 127.6f, 121.4f, 98.6f, 117.4f };
constexpr float kSpeedScale[3] = { 1.0f, 0.5f, 0.25f };
constexpr int   kPatternBeats[9] = { 8, 8, 2, 8, 6, 7, 10, 12, 16 };
constexpr int   kGraceCycles = 3;

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

    struct Tick { float pos; const char* label; };
    const Tick ticks[] = {
        { 0.000f, "-20" }, { 0.300f, "-10" }, { 0.432f, "-7" },
        { 0.520f,  "-5" }, { 0.610f,  "-3" }, { 0.647f, "-2" },
        { 0.683f,  "-1" }, { 0.720f,   "0" }, { 0.813f, "+1" },
        { 0.907f,  "+2" }, { 1.000f,  "+3" },
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

    g.setFont(sg9Font(11.0f * w / 132.0f));
    for (const auto& t : ticks) {
        const float a = angleFor(t.pos);
        const float ca = std::cos(a), sa = std::sin(a);
        const bool hot = t.pos >= kZeroPos - 1e-6f;
        const juce::Colour c = hot ? hotZone : warmWhite;
        g.setColour(c.withAlpha(0.9f));
        g.drawLine(cx + ca * r, cy + sa * r,
                   cx + ca * (r - 9.0f), cy + sa * (r - 9.0f), 2.0f);
        const float nr = r + 16.0f;
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

    g.setFont(sg9Font(10.0f * w / 132.0f));
    g.setColour(juce::Colour(0x66ffffff));
    g.drawText(left_ ? "L" : "R", int(cx) + 10, int(cy) - 24, 24, 16,
               juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
// IncenseOverlay
// ---------------------------------------------------------------------------

IncenseOverlay::IncenseOverlay() {
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void IncenseOverlay::setPhotoRect(float x, float y, float w, float h) {
    photoX_ = x; photoY_ = y; photoW_ = w; photoH_ = h;
}

void IncenseOverlay::timerCallback() {
    t_ += 1.0 / 30.0;
    repaint();
}

juce::Point<float> IncenseOverlay::toScreen(float nx, float ny) const {
    return { photoX_ + nx * photoW_, photoY_ + ny * photoH_ };
}

void IncenseOverlay::paint(juce::Graphics& g) {
    const float sx = photoW_ / 1170.0f;   // photo-px -> screen-px
    const float sy = photoH_ / 1413.0f;
    const float sAvg = (sx + sy) * 0.5f;
    const double twoPi = 6.283185307179586;

    for (int s = 0; s < 2; ++s) {
        const auto& st = kSticks[s];
        const float emberNX = st.x1;
        const auto p0 = toScreen(st.x0, st.y0); // stick base (in the hand)
        const auto p1 = toScreen(st.x1, st.y1); // ember tip

        // Faint haze column behind the smoke, drifting almost imperceptibly.
        {
            const float hxN = emberNX + (8.0f / 1170.0f)
                * float(std::sin(twoPi * t_ / 15.0 + s * 2.0));
            const auto hc = toScreen(hxN, kHazeNY);
            const float hrx = 118.0f * sx, hry = 274.0f * sy;
            const float ha = juce::jlimit(0.0f, 0.08f,
                0.035f + 0.012f * float(std::sin(twoPi * t_ / 15.0 + 1.0 + s * 2.0)));
            juce::ColourGradient hg(juce::Colour(0xffb0b0b0).withAlpha(ha),
                                    hc.x, hc.y,
                                    juce::Colour(0x00b0b0b0),
                                    hc.x + hrx, hc.y, true);
            g.setGradientFill(hg);
            g.fillEllipse(hc.x - hrx, hc.y - hry, hrx * 2.0f, hry * 2.0f);
        }

        // Wooden stick with a lighter highlight edge.
        g.setColour(juce::Colour(0xff7c4a24));
        g.drawLine(p0.x, p0.y, p1.x, p1.y, 5.0f * sx);
        g.setColour(juce::Colour(0xffb07a3f));
        g.drawLine(p0.x + 1.2f * sx, p0.y, p1.x + 1.2f * sx, p1.y, 1.4f * sx);

        // Eight smoke puffs per stick: small at the ember, growing as they
        // rise, drifting side to side, fading out, then looping.
        for (int i = 0; i < 8; ++i) {
            const double p = std::fmod(t_ / 14.0 + i / 8.0 + s * 0.5, 1.0);
            const float pf = (float) p;
            const float cyN = kSmokeY0 - pf * (kSmokeY0 - kSmokeY1);
            const float driftN = float(std::sin(pf * twoPi + i * 0.9 + s * 2.1))
                * (8.0f + 36.0f * pf) / 1170.0f;
            const auto c = toScreen(emberNX + driftN, cyN);
            const float rx = (22.0f + pf * 108.0f) * sx;
            const float ry = (14.0f + pf * 64.0f) * sy;
            const float a = 0.13f
                * std::pow(std::sin(float(3.14159265 * pf)), 0.8f);
            if (a <= 0.001f) continue;
            juce::ColourGradient pg(juce::Colour(0xffc2c2c2).withAlpha(a),
                                    c.x, c.y,
                                    juce::Colour(0x00c2c2c2),
                                    c.x + rx, c.y, true);
            g.setGradientFill(pg);
            g.fillEllipse(c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f);
        }

        // Ember: layered glowing dot, gently pulsing (~3.2 s cycle).
        {
            const float pulse = 0.75f + 0.25f * float(std::sin(twoPi * t_ / 3.2));
            const auto e = toScreen(emberNX, kEmberNY);
            g.setColour(juce::Colour(0xffff7a2a).withAlpha(0.28f * pulse));
            g.fillEllipse(e.x - 13.0f * sAvg, e.y - 13.0f * sAvg,
                          26.0f * sAvg, 26.0f * sAvg);
            g.setColour(juce::Colour(0xffff8c2e).withAlpha(0.65f * pulse));
            g.fillEllipse(e.x - 6.5f * sAvg, e.y - 6.5f * sAvg,
                          13.0f * sAvg, 13.0f * sAvg);
            g.setColour(juce::Colour(0xffffb347).withAlpha(0.95f));
            g.fillEllipse(e.x - 3.0f * sAvg, e.y - 3.0f * sAvg,
                          6.0f * sAvg, 6.0f * sAvg);
        }
    }
}

// ---------------------------------------------------------------------------
// Sg9Editor
// ---------------------------------------------------------------------------

Sg9Editor::Sg9Editor(Sg9Processor& proc)
    : juce::AudioProcessorEditor(proc), proc_(proc),
      meterL_(proc, true), meterR_(proc, false),
      pulseButton_("Pulse") {
    setSize(880, 720);
    setResizable(true, true);
    // Lock the design aspect: resizing scales the 880x720 layout instead
    // of reflowing it, so controls can never end up in weird places.
    constrainer_.setFixedAspectRatio(880.0 / 720.0);
    constrainer_.setMinimumSize(550, 450);
    constrainer_.setMaximumSize(1320, 1080);
    setConstrainer(&constrainer_);

    photo_ = juce::ImageFileFormat::loadFrom(Sg9Assets::chakrafigure_png,
                                             (size_t) Sg9Assets::chakrafigure_pngSize);

    addAndMakeVisible(meterL_);
    addAndMakeVisible(meterR_);
    addAndMakeVisible(incense_); // above the figure, below the controls

    for (int i = 0; i < 5; ++i) {
        faderSliders_[i].setSliderStyle(juce::Slider::LinearVertical);
        faderSliders_[i].setTextBoxStyle(juce::Slider::TextBoxBelow,
                                         false, 44, 18);
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

    lastPadVoicing_ = choiceIndex("pad_voicing", 7);
    startTimerHz(30);
}

Sg9Editor::~Sg9Editor() = default;

int Sg9Editor::choiceIndex(const char* paramId, int numChoices) const {
    if (auto* p = proc_.apvts.getParameter(paramId)) {
        const float v = p->getValue(); // 0..1 normalized
        return juce::jlimit(0, numChoices - 1,
                             int(std::round(v * float(numChoices - 1))));
    }
    return 0;
}

bool Sg9Editor::inGracePeriod() const {
    if (proc_.getActiveTrigger() < 0)
        return false;
    if (auto* pp = proc_.apvts.getParameter("pulse_on"))
        if (pp->getValue() < 0.5f)
            return false;
    const float bpm = kTempoBpm[choiceIndex("tempo_source", 5)]
                    * kSpeedScale[choiceIndex("beat_speed", 3)];
    if (bpm <= 0.0f)
        return false;
    const double graceMs = double(kGraceCycles)
        * kPatternBeats[choiceIndex("drum_pattern", 9)] * 60000.0 / bpm;
    const uint32_t now = juce::Time::getMillisecondCounter();
    return (now - lastTriggerMs_) < uint32_t(graceMs);
}

juce::Point<float> Sg9Editor::photoToScreen(float nx, float ny) const {
    return { photoX_ + nx * photoW_, photoY_ + ny * photoH_ };
}

int Sg9Editor::zoneAt(float x, float y) const {
    for (int i = 0; i < 9; ++i) {
        const auto c = photoToScreen(kZoneNX[i], kZoneNY[i]);
        const float dx = x - c.x, dy = y - c.y;
        if (dx * dx + dy * dy <= hitR_ * hitR_)
            return i;
    }
    return -1;
}

void Sg9Editor::resized() {
    const float fW = (float) getWidth(), fH = (float) getHeight();

    // Scale-to-fit: the design is authored at 880x720. The constrainer
    // keeps user resizing on-aspect, but a host can impose any size (e.g.
    // the standalone restoring a previous window) — scaling the whole
    // design and centering it means the layout never jumbles.
    const float s = juce::jmin(fW / 880.0f, fH / 720.0f);
    uiScale_ = s;
    const float dX = (fW - 880.0f * s) * 0.5f; // letterbox offset
    const float dY = (fH - 720.0f * s) * 0.5f;

    // Design-space constants (880x720), scaled to screen.
    const float stripH = 172.8f * s;
    stripTop_ = dY + 547.2f * s;

    // The figure fills the space above the strip so every chakra stays
    // clear of (and tappable above) the controls.
    photoH_ = 531.2f * s;
    photoW_ = photoH_ * (1392.0f / 1680.0f);
    photoX_ = dX + (880.0f * s - photoW_) * 0.5f;
    photoY_ = dY + 8.0f * s;
    hitR_ = 42.0f * (photoH_ / 780.0f);

    incense_.setBounds(0, 0, (int) fW, (int) fH);
    incense_.setPhotoRect(photoX_, photoY_, photoW_, photoH_);

    // VU meters flanking the head.
    const float mw = 132.0f * s, mh = 93.7f * s;
    meterL_.setBounds((int) (dX + 6.0f * s), (int) (dY + 52.0f * s),
                      (int) mw, (int) mh);
    meterR_.setBounds((int) (dX + 874.0f * s - mw), (int) (dY + 52.0f * s),
                      (int) mw, (int) mh);

    // Five fader columns; the utility column (Pulse + readout) on the right.
    const float colsW = 576.8f * s;
    const float colW = 115.36f * s;
    const float x0 = dX + 24.0f * s;
    const float sliderW = 50.0f * s;
    for (int i = 0; i < 5; ++i) {
        const float cx = x0 + i * colW;
        faderLabelRects_[i] = { cx, stripTop_ + 6.0f * s, colW, 16.0f * s };
        faderSliders_[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                                         (int) (44.0f * s), (int) (18.0f * s));
        faderSliders_[i].setBounds((int) (cx + colW * 0.5f - sliderW * 0.5f),
                                   (int) (stripTop_ + 24.0f * s),
                                   (int) sliderW, (int) (96.0f * s));
        selLabelRects_[i] = { cx, stripTop_ + 122.0f * s, colW, 13.0f * s };
        selBoxes_[i].setBounds((int) (cx + 5.0f * s),
                               (int) (stripTop_ + 136.0f * s),
                               (int) (colW - 10.0f * s), (int) (26.0f * s));
    }

    const float ux = x0 + colsW + 20.0f * s;
    pulseButton_.setBounds((int) ux, (int) (stripTop_ + 24.0f * s),
                           (int) (130.0f * s), (int) (32.0f * s));
    readoutRect_ = { ux, stripTop_ + 64.0f * s,
                     juce::jmax(120.0f * s, dX + 864.0f * s - ux),
                     stripH - 72.0f * s };
}

void Sg9Editor::timerCallback() {
    // A pad-voicing change restarts the DSP's pulse grace — track it so the
    // readout stays honest.
    const int pv = choiceIndex("pad_voicing", 7);
    if (pv != lastPadVoicing_) {
        lastPadVoicing_ = pv;
        lastTriggerMs_ = juce::Time::getMillisecondCounter();
    }
    repaint(); // zone glows, incense phase and readout follow live state
}

void Sg9Editor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    // The photo IS the interface.
    if (photo_.isValid()) {
        g.drawImage(photo_, juce::Rectangle<float>(photoX_, photoY_, photoW_, photoH_));
    } else {
        g.setColour(juce::Colour(0xff141414));
        g.fillRect(juce::Rectangle<float>(photoX_, photoY_, photoW_, photoH_));
        g.setFont(uiFont(24.0f * uiScale_));
        g.setColour(juce::Colour(0x66ffffff));
        g.drawText("Synchrophonic",
                   juce::Rectangle<float>(photoX_, photoY_, photoW_, photoH_),
                   juce::Justification::centred);
    }

    // Darken the control strip for legibility.
    g.setColour(juce::Colour(0x88000000));
    g.fillRect(0.0f, stripTop_, (float) getWidth(), (float) getHeight() - stripTop_);

    // Per-zone glow: radial gradient in the chakra color, alpha from the
    // live voice meter.
    const float glowR = kGlowRadius * uiScale_;
    for (int i = 0; i < 9; ++i) {
        const auto c = photoToScreen(kZoneNX[i], kZoneNY[i]);
        const float level = juce::jlimit(0.0f, 1.0f, proc_.getVoiceLevelMeter(i));
        const float alpha = 0.10f + 0.60f * level;
        const juce::Colour col = zoneColour(i).withAlpha(alpha);
        juce::ColourGradient glow(col, c.x, c.y,
                                  juce::Colour(0x00000000), c.x + glowR, c.y, true);
        g.setGradientFill(glow);
        g.fillEllipse(c.x - glowR, c.y - glowR, glowR * 2.0f, glowR * 2.0f);

        // Small marker dot at the zone center (discoverability).
        g.setColour(zoneColour(i).withAlpha(0.55f + 0.35f * level));
        const float dr = ((i >= 7) ? 7.0f : 5.0f) * uiScale_; // foundation dots are UI-drawn
        g.fillEllipse(c.x - dr, c.y - dr, dr * 2.0f, dr * 2.0f);
    }

    // Fader + selector labels.
    g.setFont(uiFont(11.0f * uiScale_));
    g.setColour(juce::Colour(0x99ffffff));
    for (int i = 0; i < 5; ++i) {
        g.drawText(kFaderNames[i], faderLabelRects_[i],
                   juce::Justification::centred);
    }
    g.setFont(uiFont(9.0f * uiScale_));
    g.setColour(juce::Colour(0x77ffffff));
    for (int i = 0; i < 5; ++i) {
        g.drawText(kSelNames[i], selLabelRects_[i],
                   juce::Justification::centred);
    }

    // Voice + grace readout (right utility column, display only).
    {
        g.setColour(juce::Colour(0xa6000000));
        g.fillRoundedRectangle(readoutRect_, 9.0f);
        g.setColour(juce::Colour(0x40ffffff));
        g.drawRoundedRectangle(readoutRect_, 9.0f, 1.0f);

        const int sv = juce::jlimit(0, 8, selectedVoice_);
        g.setFont(uiFont(14.0f * uiScale_));
        g.setColour(juce::Colour(0xffffffff));
        g.drawText(juce::String(Sg9Processor::voiceName(sv)) + " - " +
                   juce::String(int(Sg9Processor::voiceFreq(sv))) + " Hz",
                   readoutRect_.getX(), readoutRect_.getY() + 6.0f * uiScale_,
                   readoutRect_.getWidth(), 22.0f * uiScale_,
                   juce::Justification::centred);

        float lvl = 58.0f;
        if (auto* p = proc_.apvts.getParameter(voiceLevelId(sv)))
            lvl = p->convertFrom0to1(p->getValue()); // 0..100 natural range
        g.setFont(uiFont(12.0f * uiScale_));
        g.setColour(zoneColour(sv));
        g.drawText("Level " + juce::String(int(lvl + 0.5f)) + "%",
                   readoutRect_.getX(), readoutRect_.getY() + 30.0f * uiScale_,
                   readoutRect_.getWidth(), 18.0f * uiScale_,
                   juce::Justification::centred);

        g.setFont(uiFont(11.0f * uiScale_));
        const int active = proc_.getActiveTrigger();
        if (active < 0) {
            g.setColour(juce::Colour(0x66ffffff));
            g.drawText("TAP A CHAKRA",
                       readoutRect_.getX(), readoutRect_.getY() + 52.0f * uiScale_,
                       readoutRect_.getWidth(), 18.0f * uiScale_,
                       juce::Justification::centred);
        } else if (inGracePeriod()) {
            g.setColour(juce::Colour(0xffffb347));
            g.drawText("WAITING - BREATH",
                       readoutRect_.getX(), readoutRect_.getY() + 52.0f * uiScale_,
                       readoutRect_.getWidth(), 18.0f * uiScale_,
                       juce::Justification::centred);
        } else {
            bool pulseOn = true;
            if (auto* pp = proc_.apvts.getParameter("pulse_on"))
                pulseOn = pp->getValue() >= 0.5f;
            g.setColour(pulseOn ? juce::Colour(0xff9fd8a8)
                                : juce::Colour(0x66ffffff));
            g.drawText(pulseOn ? "PULSE ON" : "PULSE OFF",
                       readoutRect_.getX(), readoutRect_.getY() + 52.0f * uiScale_,
                       readoutRect_.getWidth(), 18.0f * uiScale_,
                       juce::Justification::centred);
        }
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
        if (proc_.getActiveTrigger() == z) {
            proc_.requestRelease(z);
        } else {
            proc_.requestTrigger(z);
            lastTriggerMs_ = juce::Time::getMillisecondCounter();
        }
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
