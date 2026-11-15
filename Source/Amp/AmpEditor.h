// Ironcurve Amp - Editor: nur Presets anklicken, sonst nichts
#pragma once

#include "AmpProcessor.h"
#include "../Shared/EqDisplay.h"

class AmpEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmpEditor (AmpProcessor&);
    ~AmpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int presetAt (juce::Point<int>) const;

    AmpProcessor& proc;
    IroncurveLookAndFeel lnf;
    std::vector<juce::Rectangle<int>> cards;
    juce::Rectangle<int> guitarArea, bassArea, monoBtn, stereoBtn, tuneDownBtn, tuneUpBtn, tuneArea;
    int shownPreset = -1, hover = -1;
    bool shownStereo = false;
    int shownTune = 99;
    void stepTune (int delta);
    void setStereo (bool);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};
