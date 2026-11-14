#include "AmpEditor.h"
#include "../Shared/Theme.h"

AmpEditor::AmpEditor (AmpProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    auto setMode = [this] (float v)
    {
        if (auto* prm = proc.apvts.getParameter ("mode"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    guitarButton.onClick = [setMode] { setMode (0.0f); };
    bassButton.onClick = [setMode] { setMode (1.0f); };
    for (auto* b : { &guitarButton, &bassButton })
    {
        b->setColour (juce::TextButton::buttonColourId, Colours_::panel);
        addAndMakeVisible (*b);
    }

    cabButton.setButtonText ("BOX");
    cabButton.setTooltip (juce::String::fromUTF8 ("Eingebaute Lautsprecher-Simulation. Aus, wenn du danach eine eigene Box/IR nutzt."));
    cabButton.setColour (juce::ToggleButton::textColourId, Colours_::text);
    cabButton.setColour (juce::ToggleButton::tickColourId, Colours_::accent);
    addAndMakeVisible (cabButton);
    cabAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "cab", cabButton);

    using S = juce::String;
    addKnob ("input", "EINGANG", true, true, S::fromUTF8 ("Pegel ins Plugin. Höher = mehr Verzerrung"));
    addKnob ("gate", "GATE", true, true, S::fromUTF8 ("Schneidet Rauschen in Pausen ab. 0 = aus"));
    addKnob ("tight", "TIGHT", true, false, S::fromUTF8 ("Strafft den Bass vor der Verzerrung – für schnelle Chugs höher"));
    addKnob ("gain", "GAIN", true, true, S::fromUTF8 ("Stärke der Verzerrung"));
    addKnob ("grit", "GRIT", false, true, S::fromUTF8 ("Anteil des verzerrten Bands (Knurren)"));
    addKnob ("xover", "WEICHE", false, true, S::fromUTF8 ("Darunter bleibt der Bass sauber, darüber wird verzerrt"));
    addKnob ("lowlevel", "TIEFBAND", false, true, S::fromUTF8 ("Lautstärke des sauberen Fundaments"));
    addKnob ("bass", "BASS", true, true, S::fromUTF8 ("Tiefen / beim Bass: Körper des verzerrten Bands"));
    addKnob ("mid", "MITTEN", true, true, S::fromUTF8 ("Mitten / beim Bass: Knurren"));
    addKnob ("treble", S::fromUTF8 ("HÖHEN"), true, true, S::fromUTF8 ("Höhen"));
    addKnob ("presence", "PRESENCE", true, false, S::fromUTF8 ("Biss und Glanz der Endstufe"));
    addKnob ("depth", "DEPTH", true, false, S::fromUTF8 ("Druck im Tiefbass der Endstufe"));
    addKnob ("level", "AUSGANG", true, true, S::fromUTF8 ("Ausgangslautstärke"));

    setSize (880, 430);
    shownBass = ! proc.isBassMode(); // erzwingt erstes Layout
    startTimerHz (30);
}

AmpEditor::~AmpEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

AmpEditor::Knob& AmpEditor::addKnob (const juce::String& id, const juce::String& text, bool guitar, bool bass, const juce::String& tip)
{
    auto k = std::make_unique<Knob>();
    k->guitar = guitar; k->bass = bass;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    k->slider.setTooltip (tip);
    addChildComponent (k->slider);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    k->label.setText (text, juce::dontSendNotification);
    k->label.setFont (font (11.5f, true));
    k->label.setColour (juce::Label::textColourId, Colours_::dimText);
    k->label.setJustificationType (juce::Justification::centred);
    addChildComponent (k->label);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_::bg);
    g.setColour (Colours_::accent);
    g.setFont (font (24.0f, true));
    g.drawText ("IRONCURVE AMP", 18, 12, 300, 28, juce::Justification::centredLeft);
    g.setColour (Colours_::dimText);
    g.setFont (font (12.0f));
    g.drawText (proc.isBassMode() ? juce::String::fromUTF8 ("Bass: sauberes Fundament + verzerrtes Knurren")
                                  : juce::String::fromUTF8 ("High-Gain für Modern & Alternative Metal"),
                18, 40, 380, 16, juce::Justification::centredLeft);

    // Gate-LED und Ausgangsmeter
    auto led = juce::Rectangle<float> (getWidth() - 160.0f, 22.0f, 12.0f, 12.0f);
    g.setColour (gateShown > 0.5f ? Colours_::result : Colours_::grid);
    g.fillEllipse (led);
    g.setColour (Colours_::dimText);
    g.setFont (font (11.0f, true));
    g.drawText ("GATE OFFEN", (int) led.getRight() + 6, (int) led.getY() - 2, 90, 16, juce::Justification::centredLeft);

    auto meter = juce::Rectangle<float> (getWidth() - 160.0f, 44.0f, 140.0f, 8.0f);
    g.setColour (Colours_::grid);
    g.fillRoundedRectangle (meter, 3.0f);
    const float db = juce::Decibels::gainToDecibels (outShown, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -1.0f ? Colours_::accent : Colours_::learned);
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * frac), 3.0f);

    g.setColour (Colours_::panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 78.0f, getWidth() - 20.0f, getHeight() - 88.0f), 10.0f);
}

void AmpEditor::resized()
{
    guitarButton.setBounds (380, 20, 120, 36);
    bassButton.setBounds (506, 20, 120, 36);
    cabButton.setBounds (640, 24, 66, 28);
    layoutKnobs();
}

void AmpEditor::layoutKnobs()
{
    const bool bassMode = proc.isBassMode();
    std::vector<Knob*> visible;
    for (auto& k : knobs)
    {
        const bool show = bassMode ? k->bass : k->guitar;
        k->slider.setVisible (show);
        k->label.setVisible (show);
        if (show) visible.push_back (k.get());
    }

    // Zwei Reihen
    const int perRow = (int) ((visible.size() + 1) / 2);
    const int areaX = 20, areaW = getWidth() - 40, rowH = 165, top = 92;
    for (size_t i = 0; i < visible.size(); ++i)
    {
        const int row = (int) i / perRow, col = (int) i % perRow;
        const int inRow = row == 0 ? perRow : (int) visible.size() - perRow;
        const int cellW = areaW / juce::jmax (1, inRow);
        auto cell = juce::Rectangle<int> (areaX + col * cellW, top + row * rowH, cellW, rowH - 10);
        visible[i]->label.setBounds (cell.removeFromTop (18));
        visible[i]->slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (100, cellW - 8), cell.getHeight()));
    }
}

void AmpEditor::timerCallback()
{
    const bool bassMode = proc.isBassMode();
    if (bassMode != shownBass)
    {
        shownBass = bassMode;
        layoutKnobs();
    }
    guitarButton.setColour (juce::TextButton::buttonColourId, bassMode ? Colours_::panel : Colours_::accent);
    bassButton.setColour (juce::TextButton::buttonColourId, bassMode ? Colours_::accent : Colours_::panel);

    gateShown = proc.getGateOpen();
    const float o = proc.getOutputLevel();
    outShown = o > outShown ? o : outShown * 0.85f;
    repaint();
}
