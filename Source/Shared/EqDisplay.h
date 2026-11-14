// Ironcurve - gemeinsame EQ-Anzeige und Look & Feel
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/AutoEqFitter.h"
#include "../SpectrumAnalyzer.h"

// Was die Anzeige vom Plugin braucht (beide Plugins implementieren das)
struct EqViewSource
{
    virtual ~EqViewSource() = default;
    virtual juce::AudioProcessorValueTreeState& getApvts() = 0;
    virtual SpectrumAnalyzer& getAnalyzer() = 0;
    virtual ironcurve::EqSettings getCurrentEqSettings() const = 0;
    virtual float getAmount() const = 0;
    virtual double getAnalysisRate() const = 0;
    virtual bool hasLearnedSpectrum() const = 0;
    virtual std::vector<double> getLearnedSpectrum() const = 0;
    virtual std::vector<double> getAlignedTarget() const = 0;
};

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
    explicit EqDisplay (EqViewSource&);

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

    EqViewSource& src;
    int hoverBand = -1, dragBand = -1, selectedBand = -1;
    std::vector<double> liveCache;

    static constexpr double minDb = -15.0, maxDb = 15.0;     // EQ-Skala
    static constexpr double specMin = -90.0, specMax = 0.0;  // Spektrum-Skala
};

