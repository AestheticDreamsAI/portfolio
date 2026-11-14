// Ironcurve Amp - Editor
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

private:
    void timerCallback() override;
    void layoutKnobs();

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        bool guitar = true, bass = true;
    };
    Knob& addKnob (const juce::String& id, const juce::String& text, bool guitar, bool bass, const juce::String& tip);

    AmpProcessor& proc;
    IroncurveLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton guitarButton { "GITARRE" }, bassButton { "BASS" };
    juce::ToggleButton cabButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabAttachment;
    std::vector<std::unique_ptr<Knob>> knobs;
    bool shownBass = false;
    float gateShown = 0.0f, outShown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};
