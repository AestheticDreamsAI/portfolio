// Ironcurve Auto EQ - Editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class IroncurveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    IroncurveLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

// Grafische EQ-Ansicht mit Spektrum und ziehbaren Baendern
class EqDisplay : public juce::Component
{
public:
    explicit EqDisplay (IroncurveProcessor&);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::String getInfoText() const;

private:
    juce::Rectangle<float> plotArea() const;
    float xForFreq (double f) const;
    double freqForX (float x) const;
    float yForEqDb (double db) const;
    double eqDbForY (float y) const;
    float yForSpecDb (double db) const;
    juce::Point<float> nodePosition (int band) const;
    int hitTestNode (juce::Point<float>) const;

    juce::Path makeSpectrumPath (const std::vector<double>& db, bool closed) const;
    void drawGrid (juce::Graphics&);

    juce::RangedAudioParameter* param (int band, const char* what) const;
    float value (int band, const char* what) const;
    void setValue (int band, const char* what, float v);

    IroncurveProcessor& proc;
    int hoverBand = -1, dragBand = -1, selectedBand = -1;
    std::vector<double> liveCache;

    static constexpr double minDb = -15.0, maxDb = 15.0;     // EQ-Skala
    static constexpr double specMin = -90.0, specMax = 0.0;  // Spektrum-Skala
};

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
