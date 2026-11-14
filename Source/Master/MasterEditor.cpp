#include "MasterEditor.h"
#include "../Shared/Theme.h"

// ===========================================================================
void MeterPanel::update()
{
    shown = proc.getMeters();
    heldLufs = shown.shortTermLufs > heldLufs ? shown.shortTermLufs : heldLufs + 0.3f * (shown.shortTermLufs - heldLufs);
    repaint();
}

void MeterPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours_::panel);
    g.fillRoundedRectangle (r, 8.0f);
    r.reduce (14.0f, 12.0f);

    // Lautheit
    g.setColour (Colours_::dimText);
    g.setFont (font (11.0f, true));
    g.drawText ("LAUTHEIT (3 s)", r.removeFromTop (16.0f), juce::Justification::centredLeft);

    const float tgt = proc.getTargetLufs();
    const bool valid = heldLufs > -70.0f;
    const float diff = heldLufs - tgt;
    const auto col = ! valid ? Colours_::dimText
                   : std::abs (diff) <= 1.0f ? Colours_::result
                   : std::abs (diff) <= 3.0f ? Colours_::learned : Colours_::accent;

    g.setColour (col);
    g.setFont (font (34.0f, true));
    g.drawText (valid ? juce::String (heldLufs, 1) : juce::String::fromUTF8 ("–"), r.removeFromTop (42.0f), juce::Justification::centredLeft);
    g.setColour (Colours_::dimText);
    g.setFont (font (12.0f));
    g.drawText ("LUFS   Ziel " + juce::String (tgt, 1), r.removeFromTop (16.0f), juce::Justification::centredLeft);

    r.removeFromTop (18.0f);

    // Gain-Reduction-Balken
    auto bar = [&] (const juce::String& name, float grDb, float maxDb)
    {
        g.setColour (Colours_::dimText);
        g.setFont (font (11.0f, true));
        auto row = r.removeFromTop (16.0f);
        g.drawText (name, row, juce::Justification::centredLeft);
        g.drawText ("-" + juce::String (grDb, 1) + " dB", row, juce::Justification::centredRight);

        auto b = r.removeFromTop (10.0f);
        g.setColour (Colours_::grid);
        g.fillRoundedRectangle (b, 3.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, grDb / maxDb);
        g.setColour (Colours_::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (b.withWidth (b.getWidth() * frac), 3.0f);
        r.removeFromTop (14.0f);
    };

    bar (juce::String::fromUTF8 ("Schärfe-Schutz"), shown.deharshGr, 10.0f);
    bar ("Glue-Kompressor", shown.compGr, 10.0f);
    bar ("Limiter", shown.limitGr, 10.0f);

    g.setColour (Colours_::dimText.withAlpha (0.8f));
    g.setFont (font (11.0f));
    g.drawFittedText (juce::String::fromUTF8 ("Grün = Ziel erreicht (±1 LU)\nLimiter dauerhaft über 4 dB = zu laut"),
                      r.removeFromTop (34.0f).toNearestInt(), juce::Justification::topLeft, 2);
}

// ===========================================================================
MasterEditor::MasterEditor (MasterProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), meters (p)
{
    setLookAndFeel (&lnf);

    targetBox.addItemList ({ "Streaming (-14 LUFS)", "Ausgewogen (-11 LUFS)", "Laut (-9 LUFS)", "Metal laut (-7 LUFS)" }, 1);
    addAndMakeVisible (targetBox);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "target", targetBox);

    learnButton.setButtonText ("LERNEN");
    learnButton.setColour (juce::TextButton::buttonColourId, Colours_::accent);
    learnButton.onClick = [this] { if (proc.isLearning()) proc.cancelLearning(); else proc.startLearning(); };
    addAndMakeVisible (learnButton);

    referenceButton.setButtonText (juce::String::fromUTF8 ("REFERENZ LADEN …"));
    referenceButton.setTooltip (juce::String::fromUTF8 ("Eine Version wählen, die dir gefällt: der Master gleicht deinen Song daran an"));
    referenceButton.setColour (juce::TextButton::buttonColourId, Colours_::panel);
    referenceButton.onClick = [this]
    {
        if (! proc.isReferenceMode())
        {
            chooseReference();
            return;
        }
        juce::PopupMenu m;
        m.addItem (1, juce::String::fromUTF8 ("Andere Referenz laden …"));
        m.addItem (2, juce::String::fromUTF8 ("Referenz entfernen (Automatik-Modus)"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&referenceButton),
                         [this] (int r) { if (r == 1) chooseReference(); else if (r == 2) proc.clearReference(); });
    };
    addAndMakeVisible (referenceButton);

    resetButton.setButtonText ("RESET");
    resetButton.setColour (juce::TextButton::buttonColourId, Colours_::panel);
    resetButton.onClick = [this] { proc.resetAll(); };
    addAndMakeVisible (resetButton);

    monoButton.setColour (juce::ToggleButton::textColourId, Colours_::dimText);
    monoButton.setColour (juce::ToggleButton::tickColourId, Colours_::accent);
    addAndMakeVisible (monoButton);
    monoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "mono_on", monoButton);

    setupKnob (eqKnob, "eq_amount", juce::String::fromUTF8 ("EQ-STÄRKE"), juce::String::fromUTF8 ("Wie stark die gelernte Klangkorrektur wirkt"));
    setupKnob (deharshKnob, "deharsh", juce::String::fromUTF8 ("SCHÄRFE-SCHUTZ"), juce::String::fromUTF8 ("Senkt 3–5 kHz nur, wenn es scharf wird (max. 10 dB)"));
    setupKnob (widthKnob, "width", "BREITE", juce::String::fromUTF8 ("Stereobreite oberhalb des Mono-Bass"));
    setupKnob (monoKnob, "mono_freq", "MONO-BASS BIS", juce::String::fromUTF8 ("Unterhalb dieser Frequenz wird alles mono (Kick/Bass druckvoller)"));
    setupKnob (glueKnob, "glue", "GLUE", juce::String::fromUTF8 ("Wie stark der Kompressor den Mix zusammenhält"));
    setupKnob (ceilingKnob, "ceiling", "CEILING", juce::String::fromUTF8 ("Höchster erlaubter Pegel am Ausgang"));

    statusLabel.setFont (font (12.5f));
    statusLabel.setColour (juce::Label::textColourId, Colours_::text);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (display);
    addAndMakeVisible (meters);

    setSize (1080, 680);
    startTimerHz (30);
}

MasterEditor::~MasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MasterEditor::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Referenz wählen (eine Version, die dir gefällt)"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  proc.loadReference (f);
                          });
}

void MasterEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text, const juce::String& tooltip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setTooltip (tooltip);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setFont (font (11.0f, true));
    k.label.setColour (juce::Label::textColourId, Colours_::dimText);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);
}

void MasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_::bg);
    g.setColour (Colours_::accent);
    g.setFont (font (24.0f, true));
    g.drawText ("IRONCURVE MASTER", 18, 12, 300, 28, juce::Justification::centredLeft);
    g.setColour (Colours_::dimText);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("Automatischer Mix-Bus für Modern & Alternative Metal"), 18, 40, 340, 16, juce::Justification::centredLeft);
    g.setFont (font (11.0f, true));
    g.drawText ("ZIEL-LAUTHEIT", 380, 10, 160, 14, juce::Justification::centredLeft);
}

void MasterEditor::resized()
{
    targetBox.setBounds (380, 26, 200, 30);
    learnButton.setBounds (594, 20, 200, 42);
    resetButton.setBounds (806, 26, 70, 30);
    referenceButton.setBounds (886, 26, 184, 30);

    const int top = 76, bottom = getHeight() - 40, knobRowH = 110;
    meters.setBounds (getWidth() - 260, top, 250, bottom - top - knobRowH - 8);
    display.setBounds (10, top, getWidth() - 280, bottom - top - knobRowH - 8);

    auto row = juce::Rectangle<int> (10, bottom - knobRowH, getWidth() - 20, knobRowH);
    Knob* knobs[] = { &eqKnob, &deharshKnob, &widthKnob, &monoKnob, &glueKnob, &ceilingKnob };
    const int w = row.getWidth() / 6;
    for (auto* k : knobs)
    {
        auto cell = row.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell.reduced (w / 2 - 50, 0));
    }
    // Mono-Schalter unter das Label der Mono-Spalte
    monoButton.setBounds (monoKnob.label.getX() + monoKnob.label.getWidth() / 2 + 44, monoKnob.label.getBottom() + 4, 110, 22);

    statusLabel.setBounds (16, getHeight() - 36, getWidth() - 32, 30);
}

void MasterEditor::timerCallback()
{
    if (proc.isLearning())
        learnButton.setButtonText ("STOPP  " + proc.getPhaseName() + " " + juce::String (juce::roundToInt (proc.getLearnProgress() * 100.0f)) + " %");
    else
        learnButton.setButtonText ("LERNEN");

    auto info = display.getInfoText();
    if (info.isEmpty())
    {
        info = proc.getStatusText();
        if (proc.isLearning() && ! proc.getAnalyzer().hasRecentSignal())
            info = juce::String::fromUTF8 ("Warte auf Signal … starte die Wiedergabe in FL Studio.");
    }
    statusLabel.setText (info, juce::dontSendNotification);

    // Referenz-Modus: nur EQ, Lautheit und Limiter sind aktiv
    const bool ref = proc.isReferenceMode();
    referenceButton.setButtonText (ref ? juce::String::fromUTF8 ("REF: ") + proc.getReferenceName()
                                       : juce::String::fromUTF8 ("REFERENZ LADEN …"));
    referenceButton.setColour (juce::TextButton::buttonColourId, ref ? Colours_::result.darker (0.6f) : Colours_::panel);
    for (juce::Component* c : { (juce::Component*) &targetBox, (juce::Component*) &deharshKnob.slider, (juce::Component*) &widthKnob.slider,
                                (juce::Component*) &monoKnob.slider, (juce::Component*) &glueKnob.slider, (juce::Component*) &monoButton })
    {
        c->setEnabled (! ref);
        c->setAlpha (ref ? 0.35f : 1.0f);
    }

    meters.update();
    display.repaint();
}
