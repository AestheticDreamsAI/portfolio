#include "EqChain.h"
#include "EqParams.h"

using namespace ironcurve;

namespace
{
    // Stufe -> Band (HPF und LPF haben je zwei Biquads)
    constexpr int stageBand (int stage)
    {
        return stage <= 1 ? kHpf : (stage >= kNumBands ? kLpf : stage - 1);
    }
}

void EqChain::attach (juce::AudioProcessorValueTreeState& apvts)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& bp = bandParams[(size_t) b];
        bp.on = apvts.getRawParameterValue (eqparams::bandId (b, "on"));
        bp.freq = apvts.getRawParameterValue (eqparams::bandId (b, "freq"));
        if (bandHasGain (b))
        {
            bp.gain = apvts.getRawParameterValue (eqparams::bandId (b, "gain"));
            bp.q = apvts.getRawParameterValue (eqparams::bandId (b, "q"));
        }
    }
}

void EqChain::prepare (double sampleRate, float amount)
{
    fs = sampleRate;

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& bp = bandParams[(size_t) b];
        const bool on = bp.on->load() > 0.5f;

        freqSmooth[(size_t) b].reset (sampleRate, 0.05);
        qSmooth[(size_t) b].reset (sampleRate, 0.05);
        gainSmooth[(size_t) b].reset (sampleRate, 0.05);

        float freq = bp.freq->load();
        if (b == kHpf && ! on) freq = 10.0f;
        if (b == kLpf && ! on) freq = (float) (sampleRate * 0.49);
        freqSmooth[(size_t) b].setCurrentAndTargetValue (freq);

        if (bandHasGain (b))
        {
            qSmooth[(size_t) b].setCurrentAndTargetValue (bp.q->load());
            gainSmooth[(size_t) b].setCurrentAndTargetValue (on ? bp.gain->load() * amount : 0.0f);
        }
        cutActive[(size_t) b] = on;
    }

    for (auto& ch : filters)
        for (auto& f : ch)
            f.reset();

    stageActive.fill (false);
    updateFilters (true);
}

void EqChain::updateFilters (bool force)
{
    for (int stage = 0; stage < kNumStages; ++stage)
    {
        const int b = stageBand (stage);
        const auto type = bandType (b);
        bool active;

        if (type == FilterType::HighPass || type == FilterType::LowPass)
        {
            active = cutActive[(size_t) b];
            const bool first = (stage == 0 || stage == kNumBands);
            coeffs[(size_t) stage] = makeCoeffs (type, freqSmooth[(size_t) b].getCurrentValue(),
                                                 first ? kButterQ1 : kButterQ2, 0.0, fs);
        }
        else
        {
            const float g = gainSmooth[(size_t) b].getCurrentValue();
            active = std::abs (g) > 1.0e-4f;
            if (active || force)
                coeffs[(size_t) stage] = makeCoeffs (type, freqSmooth[(size_t) b].getCurrentValue(),
                                                     qSmooth[(size_t) b].getCurrentValue(), g, fs);
        }

        if (active && ! stageActive[(size_t) stage])
            for (auto& ch : filters)
                ch[(size_t) stage].reset();

        stageActive[(size_t) stage] = active;
        for (auto& ch : filters)
            ch[(size_t) stage].c = coeffs[(size_t) stage];
    }
}

void EqChain::process (juce::AudioBuffer<float>& buffer, int numCh, float amount) noexcept
{
    const int numSamples = buffer.getNumSamples();
    numCh = juce::jmin (numCh, 2);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bp = bandParams[(size_t) b];
        const bool on = bp.on->load() > 0.5f;

        if (b == kHpf || b == kLpf)
        {
            const float parked = b == kHpf ? 10.0f : (float) (fs * 0.49);
            if (on) cutActive[(size_t) b] = true;
            freqSmooth[(size_t) b].setTargetValue (on ? bp.freq->load() : parked);
        }
        else
        {
            freqSmooth[(size_t) b].setTargetValue (bp.freq->load());
            qSmooth[(size_t) b].setTargetValue (bp.q->load());
            gainSmooth[(size_t) b].setTargetValue (on ? bp.gain->load() * amount : 0.0f);
        }
    }

    constexpr int subBlock = 32;
    for (int pos = 0; pos < numSamples; pos += subBlock)
    {
        const int len = juce::jmin (subBlock, numSamples - pos);

        bool changed = false;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (freqSmooth[(size_t) b].isSmoothing()) { changed = true; freqSmooth[(size_t) b].skip (len); }
            if (qSmooth[(size_t) b].isSmoothing())    { changed = true; qSmooth[(size_t) b].skip (len); }
            if (gainSmooth[(size_t) b].isSmoothing()) { changed = true; gainSmooth[(size_t) b].skip (len); }
        }

        // Abgeschaltete Cuts erst deaktivieren, wenn sie aus dem Hoerbereich gefahren sind
        for (int b : { (int) kHpf, (int) kLpf })
            if (cutActive[(size_t) b] && bandParams[(size_t) b].on->load() < 0.5f && ! freqSmooth[(size_t) b].isSmoothing())
            {
                cutActive[(size_t) b] = false;
                changed = true;
            }

        if (changed)
            updateFilters (false);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch, pos);
            auto& chain = filters[(size_t) ch];

            for (int stage = 0; stage < kNumStages; ++stage)
            {
                if (! stageActive[(size_t) stage])
                    continue;
                auto& f = chain[(size_t) stage];
                for (int i = 0; i < len; ++i)
                    data[i] = f.process (data[i]);
            }
        }
    }
}

EqSettings EqChain::getSettings() const
{
    EqSettings s = EqSettings::flat();
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bp = bandParams[(size_t) b];
        auto& band = s.bands[(size_t) b];
        band.enabled = bp.on->load() > 0.5f;
        band.freq = bp.freq->load();
        if (bandHasGain (b))
        {
            band.gainDb = bp.gain->load();
            band.q = bp.q->load();
        }
    }
    return s;
}
