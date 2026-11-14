// Ironcurve Master - Editor
#pragma once

#include "MasterProcessor.h"
#include "../Shared/EqDisplay.h"

class MeterPanel : public juce::Component
{
public:
    explicit MeterPanel (MasterProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();

private:
    MasterProcessor& proc;
    MasterProcessor::Meters shown;
    float heldLufs = -100.0f;
};

class MasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterEditor (MasterProcessor&);
    ~MasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob& k, const juce::String& paramId, const juce::String& text, const juce::String& tooltip);

    MasterProcessor& proc;
    IroncurveLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox targetBox;
    juce::TextButton learnButton, resetButton;
    juce::ToggleButton monoButton { "Mono-Bass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttachment;

    Knob eqKnob, deharshKnob, widthKnob, monoKnob, glueKnob, ceilingKnob;
    juce::Label statusLabel;
    EqDisplay display;
    MeterPanel meters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEditor)
};
