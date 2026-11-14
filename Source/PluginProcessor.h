// Ironcurve Auto EQ - AudioProcessor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/AutoEqFitter.h"
#include "DSP/Biquad.h"
#include "DSP/EqModel.h"
#include "SpectrumAnalyzer.h"

#include <array>

class IroncurveProcessor : public juce::AudioProcessor
{
public:
    IroncurveProcessor();
    ~IroncurveProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Auto-EQ (Message-Thread) ----
    static constexpr double learnSeconds = 8.0;
    void startLearning();
    void cancelLearning();
    void refit();          // mit gelerntem Spektrum und aktuellem Modus neu berechnen
    void resetEq();

    bool hasLearnedSpectrum() const;
    std::vector<double> getLearnedSpectrum() const;
    std::vector<double> getAlignedTarget() const;
    juce::String getStatusText() const { return statusText; }

    ironcurve::EqSettings getCurrentEqSettings() const; // aus den Parametern
    float getAmount() const { return amountParam->load(); }
    double getAnalysisRate() const { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }

    SpectrumAnalyzer& getAnalyzer() { return analyzer; }

    juce::AudioProcessorValueTreeState apvts;

    static juce::String bandId (int band, const char* what) { return "b" + juce::String (band) + "_" + what; }
    static juce::String bandName (int band);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyFitResult (const ironcurve::FitResult& r);
    void setParam (const juce::String& id, float value);
    void updateFilters (bool force);

    SpectrumAnalyzer analyzer;

    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
    };
    std::array<BandParams, ironcurve::kNumBands> bandParams;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* modeParam = nullptr;

    // Geglaettete Werte gegen Zipper-Rauschen bei Automation
    using MulSmooth = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    using LinSmooth = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    std::array<MulSmooth, ironcurve::kNumBands> freqSmooth, qSmooth;
    std::array<LinSmooth, ironcurve::kNumBands> gainSmooth;
    LinSmooth outputSmooth;
    std::array<bool, ironcurve::kNumBands> cutActive {};

    // Stufen: HPF(2) + 8 Glocken/Shelves + LPF(2)
    static constexpr int kNumStages = ironcurve::kNumBands + 2;
    std::array<ironcurve::BiquadCoeffs, kNumStages> coeffs {};
    std::array<bool, kNumStages> stageActive {};
    std::array<std::array<ironcurve::Biquad, kNumStages>, 2> filters {};

    mutable juce::CriticalSection learnedLock;
    std::vector<double> learnedDb, alignedTargetDb;
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IroncurveProcessor)
};
