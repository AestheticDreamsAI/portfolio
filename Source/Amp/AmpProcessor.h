// Ironcurve Amp - eigener High-Gain-Verstaerker fuer Gitarre und Bass
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/AmpDSP.h"
#include "../DSP/AmpV2.h"
#include "AmpPresets.h"
#include <juce_dsp/juce_dsp.h>
#include "signalsmith-stretch.h"

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

    bool isBassMode() const;
    int getTranspose() const { return (int) std::lround (transpose->load()); }
    bool forceNoCab = false; // nur fuer Messwerkzeuge
    bool isCabReady() const { return cabConv.getCurrentIRSize() > 0; }
    bool isStereo() const { return stereo->load() > 0.5f; }
    int getPresetIndex() const;
    float getInputGainDb() const { return (float) juce::Decibels::gainToDecibels (inGain); }
    float getGateOpen() const { return gateMeter.load(); }
    float getOutputLevel() const { return outMeter.exchange (0.0f); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ironcurve::AmpParams readParams() const;

    std::array<ironcurve::GuitarAmpChannel2, 2> guitar;
    std::array<ironcurve::BassAmpChannel, 2> bass;
    ironcurve::NoiseGate gates[2];
    double midEnergy = 0.0, sideEnergy = 0.0;

    double targetGain() const;
    static constexpr double kRefPeakGuitarDb = -5.3, kRefPeakBassDb = -7.8; // Spitzenpegel, fuer die die Presets abgestimmt sind
    std::atomic<float>* preset = nullptr;
    std::atomic<float>* transpose = nullptr;
    // Tiefer stimmen (Pitch-Shifter, signalsmith-stretch, MIT-Lizenz)
    signalsmith::stretch::SignalsmithStretch<float> shifter;
    juce::AudioBuffer<float> shiftIn, shiftOut, bypassDelay;
    int shiftLatency = 0, bypassPos = 0, lastSemis = 1000;
    void applyTranspose (juce::AudioBuffer<float>&, int numCh, int n);
    juce::dsp::Convolution cabConv { juce::dsp::Convolution::Latency { 0 } };
    static juce::AudioBuffer<float> makeCabIR (double fs);
    std::atomic<float>* stereo = nullptr;
    std::vector<float> delayLine;
    ironcurve::Biquad sideHp;
    int delayPos = 0;
    double held = 0.0, inGain = 0.0, levelAttack = 0.0, levelRelease = 0.0, heldDecay = 1.0;
    std::atomic<float> gateMeter { 0.0f };
    mutable std::atomic<float> outMeter { 0.0f };
    bool lastBass = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};
