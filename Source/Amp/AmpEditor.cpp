#include "AmpEditor.h"
#include "../Shared/Theme.h"

using namespace ironcurve;

AmpEditor::AmpEditor (AmpProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    cards.resize ((size_t) kNumAmpPresets);
    setSize (900, 560);
    startTimerHz (15);
}

AmpEditor::~AmpEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AmpEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (60);
    guitarArea = area.removeFromLeft (area.getWidth() * 2 / 3);
    area.removeFromLeft (14);
    bassArea = area;

    // Gitarre: 2 Spalten x 4 Reihen, Bass: 1 Spalte x 4 Reihen
    auto layout = [this] (juce::Rectangle<int> r, int first, int count, int cols)
    {
        r.removeFromTop (26);
        const int rows = (count + cols - 1) / cols, gap = 10;
        const int cw = (r.getWidth() - gap * (cols - 1)) / cols, ch = (r.getHeight() - gap * (rows - 1)) / rows;
        for (int i = 0; i < count; ++i)
            cards[(size_t) (first + i)] = { r.getX() + (i % cols) * (cw + gap), r.getY() + (i / cols) * (ch + gap), cw, ch };
    };
    int nGuitar = 0;
    for (auto& pr : ampPresets()) if (! pr.bass) ++nGuitar;
    layout (guitarArea, 0, nGuitar, 2);
    layout (bassArea, nGuitar, kNumAmpPresets - nGuitar, 1);
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_::bg);
    g.setColour (Colours_::accent);
    g.setFont (font (24.0f, true));
    g.drawText ("IRONCURVE AMP", 18, 14, 400, 28, juce::Justification::centredLeft);
    g.setColour (Colours_::dimText);
    g.setFont (font (12.5f));
    g.drawText (juce::String::fromUTF8 ("Einfach einen Sound anklicken. Lautstärke regelt sich selbst."),
                18, 42, 600, 18, juce::Justification::centredLeft);

    g.setColour (Colours_::text);
    g.setFont (font (13.0f, true));
    g.drawText ("GITARRE", guitarArea.getX(), guitarArea.getY(), 200, 20, juce::Justification::centredLeft);
    g.drawText ("BASS", bassArea.getX(), bassArea.getY(), 200, 20, juce::Justification::centredLeft);

    const int sel = proc.getPresetIndex();
    for (int i = 0; i < kNumAmpPresets; ++i)
    {
        const auto& pr = ampPresets()[(size_t) i];
        auto r = cards[(size_t) i].toFloat();
        const bool on = i == sel;
        g.setColour (on ? Colours_::accent : (i == hover ? Colours_::panel.brighter (0.15f) : Colours_::panel));
        g.fillRoundedRectangle (r, 8.0f);
        if (! on)
        {
            g.setColour (Colours_::grid);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        }
        auto t = r.reduced (12.0f, 8.0f);
        g.setColour (on ? juce::Colours::white : Colours_::text);
        g.setFont (font (16.0f, true));
        g.drawText (juce::String::fromUTF8 (pr.name), t.removeFromTop (t.getHeight() * 0.5f), juce::Justification::bottomLeft);
        g.setColour (on ? juce::Colours::white.withAlpha (0.85f) : Colours_::dimText);
        g.setFont (font (12.0f));
        g.drawFittedText (juce::String::fromUTF8 (pr.hint), t.toNearestInt(), juce::Justification::topLeft, 2);
    }
}

int AmpEditor::presetAt (juce::Point<int> pt) const
{
    for (int i = 0; i < kNumAmpPresets; ++i)
        if (cards[(size_t) i].contains (pt)) return i;
    return -1;
}

void AmpEditor::mouseUp (const juce::MouseEvent& e)
{
    const int i = presetAt (e.getPosition());
    if (i < 0) return;
    if (auto* prm = proc.apvts.getParameter ("preset"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
        prm->endChangeGesture();
    }
    repaint();
}

void AmpEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = presetAt (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
}

void AmpEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void AmpEditor::timerCallback()
{
    const int sel = proc.getPresetIndex();
    if (sel != shownPreset) { shownPreset = sel; repaint(); }
}
