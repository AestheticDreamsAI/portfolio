// Ironcurve - die 10-Band-Filterkette mit Parameter-Glaettung (von beiden Plugins genutzt)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/Biquad.h"
#include "../DSP/EqModel.h"

#include <array>

class EqChain
{
public:
    void attach (juce::AudioProcessorValueTreeState& apvts);
    void prepare (double sampleRate, float amount = 1.0f);
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept;
    ironcurve::EqSettings getSettings() const;

private:
    void updateFilters (bool force);

    double fs = 48000.0;

    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
    };
    std::array<BandParams, ironcurve::kNumBands> bandParams;

    using MulSmooth = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    using LinSmooth = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    std::array<MulSmooth, ironcurve::kNumBands> freqSmooth, qSmooth;
    std::array<LinSmooth, ironcurve::kNumBands> gainSmooth;
    std::array<bool, ironcurve::kNumBands> cutActive {};

    // Stufen: HPF(2) + 8 Glocken/Shelves + LPF(2)
    static constexpr int kNumStages = ironcurve::kNumBands + 2;
    std::array<ironcurve::BiquadCoeffs, kNumStages> coeffs {};
    std::array<bool, kNumStages> stageActive {};
    std::array<std::array<ironcurve::Biquad, kNumStages>, 2> filters {};
};
