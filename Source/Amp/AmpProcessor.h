// Ironcurve Amp - eigener High-Gain-Verstaerker fuer Gitarre und Bass
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/AmpDSP.h"

#include <array>

class AmpProcessor : public juce::AudioProcessor
{
public:
    AmpProcessor();
    ~AmpProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ironcurve Amp"; }
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

    bool isBassMode() const { return mode->load() > 0.5f; }
    float getGateOpen() const { return gateMeter.load(); }
    float getOutputLevel() const { return outMeter.exchange (0.0f); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ironcurve::AmpParams readParams() const;

    std::array<ironcurve::GuitarAmpChannel, 2> guitar;
    std::array<ironcurve::BassAmpChannel, 2> bass;
    ironcurve::NoiseGate gate;

    std::atomic<float> *mode, *input, *gateAmt, *tight, *gain, *bassP, *mid, *treble, *presence, *depth, *level, *cab,
                       *xover, *lowLevel, *grit;
    std::atomic<float> gateMeter { 0.0f };
    mutable std::atomic<float> outMeter { 0.0f };
    bool lastBass = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};
