// Ironcurve Auto EQ - Editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Shared/EqDisplay.h"

class IroncurveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit IroncurveEditor (IroncurveProcessor&);
    ~IroncurveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    IroncurveProcessor& proc;
    IroncurveLookAndFeel lnf;

    juce::ComboBox modeBox;
    juce::TextButton learnButton, refitButton, resetButton;
    juce::Slider amountKnob, outputKnob;
    juce::Label amountLabel, outputLabel, statusLabel;
    EqDisplay display;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<SliderAttachment> amountAttachment, outputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IroncurveEditor)
};
