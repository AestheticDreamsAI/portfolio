// Ironcurve Auto EQ - AudioProcessor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/AutoEqFitter.h"
#include "Shared/EqChain.h"
#include "Shared/EqDisplay.h"
#include "Shared/EqParams.h"
#include "SpectrumAnalyzer.h"

class IroncurveProcessor : public juce::AudioProcessor, public EqViewSource
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
    void refit();
    void resetEq();
    juce::String getStatusText() const { return statusText; }

    // ---- EqViewSource ----
    juce::AudioProcessorValueTreeState& getApvts() override { return apvts; }
    SpectrumAnalyzer& getAnalyzer() override { return analyzer; }
    ironcurve::EqSettings getCurrentEqSettings() const override { return eqChain.getSettings(); }
    float getAmount() const override { return amountParam->load(); }
    double getAnalysisRate() const override { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }
    bool hasLearnedSpectrum() const override;
    std::vector<double> getLearnedSpectrum() const override;
    std::vector<double> getAlignedTarget() const override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    SpectrumAnalyzer analyzer;
    EqChain eqChain;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmooth;

    mutable juce::CriticalSection learnedLock;
    std::vector<double> learnedDb, alignedTargetDb;
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IroncurveProcessor)
};
