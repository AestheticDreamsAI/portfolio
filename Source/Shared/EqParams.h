// Ironcurve - gemeinsame Parameter-Definitionen fuer die 10 EQ-Baender
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/EqModel.h"

namespace eqparams
{
    inline juce::String bandId (int band, const char* what) { return "b" + juce::String (band) + "_" + what; }

    juce::String bandName (int band);
    juce::String formatHz (float v);
    juce::NormalisableRange<float> logRange (float lo, float hi);

    using ParamList = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;
    void addBandParameters (ParamList& params);

    // Schreibt Werte als Host-Automation (mit Gesten)
    void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value);
    void applyEq (juce::AudioProcessorValueTreeState& apvts, const ironcurve::EqSettings& eq);
}
