#include "PluginEditor.h"
#include "Shared/Theme.h"

using namespace ironcurve;

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
