#include "PluginEditor.h"

using namespace ironcurve;

namespace Colours_
{
    const juce::Colour bg        { 0xff111114 };
    const juce::Colour panel     { 0xff1a1b20 };
    const juce::Colour grid      { 0xff2a2c33 };
    const juce::Colour text      { 0xffd8d8dc };
    const juce::Colour dimText   { 0xff8a8c95 };
    const juce::Colour accent    { 0xffe2412b }; // EQ-Kurve, Buttons
    const juce::Colour learned   { 0xfff0a03c }; // gelerntes Spektrum
    const juce::Colour result    { 0xff3fc7b5 }; // Ergebnis nach EQ
    const juce::Colour live      { 0xff5d6a88 }; // Live-Analyzer
}

static juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// ===========================================================================
IroncurveLookAndFeel::IroncurveLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, Colours_::panel);
    setColour (juce::ComboBox::outlineColourId, Colours_::grid);
    setColour (juce::ComboBox::textColourId, Colours_::text);
    setColour (juce::ComboBox::arrowColourId, Colours_::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours_::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours_::accent);
    setColour (juce::TextButton::textColourOffId, Colours_::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, Colours_::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours_::text);
}

void IroncurveLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 3, radius - 3, 0, startAngle, endAngle, true);
    g.setColour (Colours_::grid);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - 3, radius - 3, 0, startAngle, angle, true);
    g.setColour (Colours_::accent);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Colours_::panel.brighter (0.15f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (c));

    const juce::Point<float> tip (c.x + std::sin (angle) * radius * 0.55f, c.y - std::cos (angle) * radius * 0.55f);
    g.setColour (Colours_::text);
    g.drawLine ({ c, tip }, 2.5f);
}

void IroncurveLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                                                 bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto col = base;
    if (down) col = col.brighter (0.2f);
    else if (over) col = col.brighter (0.1f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (Colours_::grid.brighter (0.2f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

// ===========================================================================
EqDisplay::EqDisplay (IroncurveProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
}

juce::Rectangle<float> EqDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (28.0f).withTrimmedBottom (18.0f).withTrimmedRight (30.0f);
}

float EqDisplay::xForFreq (double f) const
{
    const auto a = plotArea();
    return a.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * a.getWidth();
}

double EqDisplay::freqForX (float x) const
{
    const auto a = plotArea();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth()));
    return 20.0 * std::pow (1000.0, t);
}

float EqDisplay::yForEqDb (double db) const
{
    const auto a = plotArea();
    return a.getBottom() - (float) ((db - minDb) / (maxDb - minDb)) * a.getHeight();
}

double EqDisplay::eqDbForY (float y) const
{
    const auto a = plotArea();
    return minDb + (a.getBottom() - y) / a.getHeight() * (maxDb - minDb);
}

float EqDisplay::yForSpecDb (double db) const
{
    const auto a = plotArea();
    const double t = juce::jlimit (-0.05, 1.05, (db - specMin) / (specMax - specMin));
    return a.getBottom() - (float) t * a.getHeight();
}

juce::RangedAudioParameter* EqDisplay::param (int band, const char* what) const
{
    return proc.apvts.getParameter (IroncurveProcessor::bandId (band, what));
}

float EqDisplay::value (int band, const char* what) const
{
    if (auto* v = proc.apvts.getRawParameterValue (IroncurveProcessor::bandId (band, what)))
        return v->load();
    return 0.0f;
}

void EqDisplay::setValue (int band, const char* what, float v)
{
    if (auto* p = param (band, what))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

juce::Point<float> EqDisplay::nodePosition (int band) const
{
    const float x = xForFreq (value (band, "freq"));
    const float y = bandHasGain (band) ? yForEqDb (value (band, "gain")) : yForEqDb (0.0);
    const auto a = plotArea();
    return { juce::jlimit (a.getX(), a.getRight(), x), juce::jlimit (a.getY(), a.getBottom(), y) };
}

int EqDisplay::hitTestNode (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 13.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        float d = nodePosition (b).getDistanceFrom (p);
        if (value (b, "on") < 0.5f)
            d += 3.0f; // aktive Baender bevorzugen
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

juce::Path EqDisplay::makeSpectrumPath (const std::vector<double>& db, bool closed) const
{
    juce::Path p;
    const auto a = plotArea();
    const double nyquist = proc.getAnalysisRate() * 0.5;
    bool started = false;
    float lastX = a.getX();

    for (int i = 0; i < AnalysisGrid::kNumBands; ++i)
    {
        const double f = AnalysisGrid::centerHz (i);
        if (f >= nyquist)
            break;
        const float x = xForFreq (f);
        const float y = yForSpecDb (db[(size_t) i]);
        if (! started)
        {
            if (closed) { p.startNewSubPath (x, a.getBottom()); p.lineTo (x, y); }
            else          p.startNewSubPath (x, y);
            started = true;
        }
        else
            p.lineTo (x, y);
        lastX = x;
    }

    if (closed && started)
    {
        p.lineTo (lastX, a.getBottom());
        p.closeSubPath();
    }
    return p;
}

void EqDisplay::drawGrid (juce::Graphics& g)
{
    const auto a = plotArea();
    g.setFont (font (11.0f));

    for (double f : { 30.0, 50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xForFreq (f);
        const bool major = (f == 100.0 || f == 1000.0 || f == 10000.0);
        g.setColour (Colours_::grid.withAlpha (major ? 1.0f : 0.6f));
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        g.setColour (Colours_::dimText);
        const auto label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        g.drawText (label, juce::Rectangle<float> (x - 20, a.getBottom() + 2, 40, 14), juce::Justification::centred);
    }

    for (double db = -12.0; db <= 12.0; db += 6.0)
    {
        const float y = yForEqDb (db);
        g.setColour (db == 0.0 ? Colours_::grid.brighter (0.35f) : Colours_::grid);
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (Colours_::dimText);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (a.getRight() + 4, y - 7, 26, 14),
                    juce::Justification::centredLeft);
    }

    for (double db = -80.0; db <= -10.0; db += 20.0)
    {
        g.setColour (Colours_::dimText.withAlpha (0.6f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (a.getX() - 30, yForSpecDb (db) - 7, 26, 14),
                    juce::Justification::centredRight);
    }
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto a = plotArea();
    g.setColour (Colours_::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    drawGrid (g);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (a.toNearestInt());

    const double octaveOffset = 10.0 * std::log10 ((double) AnalysisGrid::kBandsPerOctave); // Anzeige pro Oktave
    const double fs = proc.getAnalysisRate();
    const auto eq = proc.getCurrentEqSettings();
    const double amount = proc.getAmount();

    // Live-Spektrum (Eingang, vor dem EQ)
    {
        const auto& live = proc.getAnalyzer().getDisplayDb();
        liveCache.assign (live.begin(), live.end());
        liveCache = smoothDb (liveCache, 2);
        for (auto& d : liveCache)
            d += octaveOffset;

        const auto path = makeSpectrumPath (liveCache, true);
        g.setGradientFill (juce::ColourGradient (Colours_::live.withAlpha (0.55f), 0, a.getY(),
                                                 Colours_::live.withAlpha (0.08f), 0, a.getBottom(), false));
        g.fillPath (path);
    }

    // Gelerntes Spektrum, Zielkurve und vorhergesagtes Ergebnis
    if (proc.hasLearnedSpectrum())
    {
        auto learned = smoothDb (proc.getLearnedSpectrum(), 3);
        auto target = proc.getAlignedTarget();
        std::vector<double> after (learned.size());

        for (size_t i = 0; i < learned.size(); ++i)
        {
            learned[i] += octaveOffset;
            const double f = AnalysisGrid::centerHz ((int) i);
            after[i] = learned[i] + (f < fs * 0.5 ? eqResponseDb (eq, amount, f, fs) : 0.0);
        }

        g.setColour (Colours_::learned.withAlpha (0.8f));
        g.strokePath (makeSpectrumPath (learned, false), juce::PathStrokeType (1.3f));

        if (target.size() == learned.size())
        {
            for (auto& t : target)
                t += octaveOffset;
            juce::Path dashed;
            const float dashes[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, makeSpectrumPath (target, false), dashes, 2);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillPath (dashed);
        }

        g.setColour (Colours_::result.withAlpha (0.9f));
        g.strokePath (makeSpectrumPath (after, false), juce::PathStrokeType (1.6f));
    }

    // EQ-Kurve
    {
        juce::Path curve, fill;
        const float zeroY = yForEqDb (0.0);
        bool first = true;
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const double f = freqForX (x);
            const float y = yForEqDb (f < fs * 0.5 ? eqResponseDb (eq, amount, f, fs) : -60.0);
            if (first) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, zeroY); fill.lineTo (x, y); first = false; }
            else       { curve.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (a.getRight(), zeroY);
        fill.closeSubPath();

        g.setColour (Colours_::accent.withAlpha (0.13f));
        g.fillPath (fill);
        g.setColour (Colours_::accent);
        g.strokePath (curve, juce::PathStrokeType (2.4f));
    }

    // Nodes
    g.setFont (font (10.0f, true));
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto pos = nodePosition (b);
        const bool on = value (b, "on") > 0.5f;
        const bool highlighted = (b == hoverBand || b == dragBand || b == selectedBand);
        const float r = highlighted ? 9.0f : 7.5f;
        const auto circle = juce::Rectangle<float> (r * 2, r * 2).withCentre (pos);

        const auto col = (b == kRes1 || b == kRes2) ? Colours_::learned : Colours_::accent;
        if (on)
        {
            g.setColour (col.withAlpha (highlighted ? 1.0f : 0.85f));
            g.fillEllipse (circle);
            g.setColour (juce::Colours::black);
        }
        else
        {
            g.setColour (Colours_::dimText.withAlpha (highlighted ? 0.9f : 0.45f));
            g.drawEllipse (circle, 1.5f);
        }

        const juce::String label = b == kHpf ? "L" : b == kLpf ? "H" : b == kLowShelf ? "S" : b == kHighShelf ? "S"
                                 : (b == kRes1 || b == kRes2) ? "R" : juce::String (b - kBell1 + 1);
        g.drawText (label, circle, juce::Justification::centred);
    }

    // Legende
    g.setFont (font (11.0f));
    auto legend = juce::Rectangle<float> (a.getX() + 8, a.getY() + 6, 520, 16);
    auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (legend.getX(), legend.getCentreY() - 1.5f, 14.0f, 3.0f);
        g.setColour (Colours_::dimText);
        g.drawText (t, legend.withTrimmedLeft (18).withWidth (w), juce::Justification::centredLeft);
        legend.removeFromLeft (w + 24);
    };
    item (Colours_::live, "Live-Eingang", 72);
    if (proc.hasLearnedSpectrum())
    {
        item (Colours_::learned, "Gelernt", 46);
        item (juce::Colours::white.withAlpha (0.55f), "Ziel", 28);
        item (Colours_::result, "Ergebnis", 52);
    }
    item (Colours_::accent, "EQ", 20);
}

juce::String EqDisplay::getInfoText() const
{
    const int b = dragBand >= 0 ? dragBand : (hoverBand >= 0 ? hoverBand : -1);
    if (b < 0)
        return {};

    const float f = value (b, "freq");
    juce::String s = IroncurveProcessor::bandName (b) + "   "
                   + (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
    if (bandHasGain (b))
        s << "   " << (value (b, "gain") >= 0 ? "+" : "") << juce::String (value (b, "gain"), 1) << " dB   Q "
          << juce::String (value (b, "q"), 2);
    if (value (b, "on") < 0.5f)
        s << "   (aus)";
    s << juce::String::fromUTF8 ("      Ziehen = Freq/Gain · Mausrad = Q · Doppelklick = an/aus");
    return s;
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h != hoverBand)
    {
        hoverBand = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragBand = hitTestNode (e.position);
    selectedBand = dragBand;
    if (dragBand < 0)
        return;

    for (const char* w : { "on", "freq", "gain" })
        if (auto* p = param (dragBand, w))
            p->beginChangeGesture();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    if (value (dragBand, "on") < 0.5f)
        setValue (dragBand, "on", 1.0f); // Ziehen schaltet ein Band ein

    setValue (dragBand, "freq", (float) freqForX (e.position.x));
    if (bandHasGain (dragBand))
        setValue (dragBand, "gain", (float) juce::jlimit (-18.0, 18.0, eqDbForY (e.position.y)));
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        for (const char* w : { "on", "freq", "gain" })
            if (auto* p = param (dragBand, w))
                p->endChangeGesture();
    dragBand = -1;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitTestNode (e.position);
    if (b < 0)
        return;
    if (auto* p = param (b, "on"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (value (b, "on") > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int b = hitTestNode (e.position);
    if (b < 0 || ! bandHasQ (b))
        return;

    const float factor = std::pow (1.15f, wheel.deltaY * 8.0f);
    if (auto* p = param (b, "q"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.3f, 16.0f, value (b, "q") * factor)));
        p->endChangeGesture();
    }
}

// ===========================================================================
IroncurveEditor::IroncurveEditor (IroncurveProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&lnf);

    for (const auto& m : modeProfiles())
        modeBox.addItem (juce::String::fromUTF8 (m.name), modeBox.getNumItems() + 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "mode", modeBox);

    learnButton.setButtonText ("LERNEN");
    learnButton.setColour (juce::TextButton::buttonColourId, Colours_::accent);
    learnButton.onClick = [this]
    {
        if (proc.getAnalyzer().isLearning()) proc.cancelLearning();
        else                                 proc.startLearning();
    };
    addAndMakeVisible (learnButton);

    refitButton.setButtonText ("NEU BERECHNEN");
    refitButton.setTooltip ("Gelerntes Spektrum mit dem aktuellen Modus neu auswerten");
    refitButton.setColour (juce::TextButton::buttonColourId, Colours_::panel);
    refitButton.onClick = [this] { proc.refit(); };
    addAndMakeVisible (refitButton);

    resetButton.setButtonText ("RESET");
    resetButton.setColour (juce::TextButton::buttonColourId, Colours_::panel);
    resetButton.onClick = [this] { proc.resetEq(); };
    addAndMakeVisible (resetButton);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
        addAndMakeVisible (s);
        l.setText (text, juce::dontSendNotification);
        l.setFont (font (11.0f, true));
        l.setColour (juce::Label::textColourId, Colours_::dimText);
        l.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (l);
    };
    setupKnob (amountKnob, amountLabel, juce::String::fromUTF8 ("STÄRKE"));
    setupKnob (outputKnob, outputLabel, "AUSGANG");
    amountAttachment = std::make_unique<SliderAttachment> (proc.apvts, "amount", amountKnob);
    outputAttachment = std::make_unique<SliderAttachment> (proc.apvts, "output", outputKnob);

    statusLabel.setFont (font (12.5f));
    statusLabel.setColour (juce::Label::textColourId, Colours_::text);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (display);

    setSize (1020, 620);
    startTimerHz (30);
}

IroncurveEditor::~IroncurveEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void IroncurveEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_::bg);

    g.setColour (Colours_::accent);
    g.setFont (font (24.0f, true));
    g.drawText ("IRONCURVE", 18, 12, 200, 28, juce::Justification::centredLeft);
    g.setColour (Colours_::dimText);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("Auto EQ für Modern & Alternative Metal"), 18, 40, 260, 16, juce::Justification::centredLeft);

    g.setFont (font (11.0f, true));
    g.drawText ("MODUS", 290, 10, 120, 14, juce::Justification::centredLeft);
}

void IroncurveEditor::resized()
{
    modeBox.setBounds (290, 26, 190, 30);
    learnButton.setBounds (492, 22, 150, 38);
    refitButton.setBounds (650, 26, 120, 30);
    resetButton.setBounds (776, 26, 64, 30);

    amountLabel.setBounds (852, 12, 64, 32);
    amountKnob.setBounds (912, 10, 100, 36);
    outputLabel.setBounds (852, 50, 64, 32);
    outputKnob.setBounds (912, 48, 100, 36);

    display.setBounds (10, 92, getWidth() - 20, getHeight() - 92 - 40);
    statusLabel.setBounds (16, getHeight() - 36, getWidth() - 32, 30);
}

void IroncurveEditor::timerCallback()
{
    auto& an = proc.getAnalyzer();

    if (an.isLearning())
    {
        const int pct = juce::roundToInt (an.getLearnProgress() * 100.0f);
        learnButton.setButtonText ("STOPP  " + juce::String (pct) + " %");
    }
    else
    {
        learnButton.setButtonText ("LERNEN");
    }

    auto info = display.getInfoText();
    if (info.isEmpty())
    {
        info = proc.getStatusText();
        if (an.isLearning() && ! an.hasRecentSignal())
            info = juce::String::fromUTF8 ("Warte auf Signal … starte die Wiedergabe in FL Studio.");
    }
    statusLabel.setText (info, juce::dontSendNotification);

    display.repaint();
}
