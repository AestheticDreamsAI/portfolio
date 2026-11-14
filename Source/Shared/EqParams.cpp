#include "EqParams.h"

using namespace ironcurve;

namespace eqparams
{

juce::String bandName (int band)
{
    switch (band)
    {
        case kHpf:       return "Low Cut";
        case kLowShelf:  return "Low Shelf";
        case kRes1:      return "Resonanz 1";
        case kRes2:      return "Resonanz 2";
        case kHighShelf: return "High Shelf";
        case kLpf:       return "High Cut";
        default:         return "Glocke " + juce::String (band - kBell1 + 1);
    }
}

juce::String formatHz (float v)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float t) { return s * std::pow (e / s, t); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

void addBandParameters (ParamList& params)
{
    using namespace juce;
    const auto flat = EqSettings::flat();

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& d = flat.bands[(size_t) b];
        const auto name = bandName (b);

        params.push_back (std::make_unique<AudioParameterBool> (ParameterID { bandId (b, "on"), 1 }, name + " An", d.enabled));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandId (b, "freq"), 1 }, name + " Freq", logRange (20.0f, 20000.0f), (float) d.freq,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })));

        if (bandHasGain (b))
        {
            params.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { bandId (b, "gain"), 1 }, name + " Gain", NormalisableRange<float> (-18.0f, 18.0f, 0.01f), (float) d.gainDb,
                AudioParameterFloatAttributes().withLabel ("dB")));
            params.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { bandId (b, "q"), 1 }, name + " Q", logRange (0.3f, 16.0f), (float) d.q));
        }
    }
}

void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void applyEq (juce::AudioProcessorValueTreeState& apvts, const EqSettings& eq)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& band = eq.bands[(size_t) b];
        setParam (apvts, bandId (b, "on"), band.enabled ? 1.0f : 0.0f);
        setParam (apvts, bandId (b, "freq"), (float) band.freq);
        if (bandHasGain (b))
        {
            setParam (apvts, bandId (b, "gain"), (float) band.gainDb);
            setParam (apvts, bandId (b, "q"), (float) band.q);
        }
    }
}

} // namespace eqparams
