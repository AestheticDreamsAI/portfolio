#include "AmpProcessor.h"
#include "AmpEditor.h"

using namespace ironcurve;

juce::AudioProcessorValueTreeState::ParameterLayout AmpProcessor::createLayout()
{
    using namespace juce;
    StringArray names;
    for (auto& pr : ampPresets()) names.add (String::fromUTF8 (pr.name));
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "preset", 1 }, "Preset", names, 0));
    return { p.begin(), p.end() };
}

AmpProcessor::AmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    preset = apvts.getRawParameterValue ("preset");
}

int AmpProcessor::getPresetIndex() const
{
    return juce::jlimit (0, kNumAmpPresets - 1, (int) std::lround (preset->load()));
}

bool AmpProcessor::isBassMode() const { return ampPresets()[(size_t) getPresetIndex()].bass; }

bool AmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

AmpParams AmpProcessor::readParams() const
{
    return ampPresets()[(size_t) getPresetIndex()].p;
}

void AmpProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& g : guitar) g.prepare (sampleRate);
    for (auto& b : bass) b.prepare (sampleRate);
    gate.prepare (sampleRate);
    levelAttack = 1.0 - std::exp (-1.0 / (0.002 * sampleRate));
    levelRelease = 1.0 - std::exp (-1.0 / (0.4 * sampleRate));
    heldDecay = std::pow (10.0, -0.5 / 20.0 / sampleRate); // 0,5 dB/s
    if (inGain <= 0.0) inGain = targetGain();
    const auto p = readParams();
    for (auto& g : guitar) g.setParams (p);
    for (auto& b : bass) b.setParams (p);
    lastBass = isBassMode();
    setLatencySamples (Oversampler4x::latency());
}

void AmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
    const int numCh = juce::jmin (2, numIn, buffer.getNumChannels());
    if (numCh <= 0) return;

    const bool bassMode = isBassMode();
    const auto p = readParams();
    if (bassMode != lastBass) // Moduswechsel: Zustand zuruecksetzen, damit nichts knallt
    {
        for (auto& g : guitar) g.prepare (getSampleRate());
        for (auto& b : bass) b.prepare (getSampleRate());
        lastBass = bassMode;
    }
    if (bassMode) for (auto& b : bass) b.setParams (p);
    else          for (auto& g : guitar) g.setParams (p);

    // Gate: aus bei 0, sonst Schwelle zwischen -75 dBFS (sanft) und -35 dBFS (streng)
    gate.setEnabled (p.gate > 0.01);
    gate.setThreshold (-75.0 + 40.0 * p.gate);

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float peak = 0.0f, gSum = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        // Automatischer Eingangspegel: lernt, wie laut die Gitarre/der Bass gespielt wird,
        // und bringt sie auf den Pegel, fuer den die Presets abgestimmt sind.
        const double a = std::max (std::abs ((double) L[i]), std::abs ((double) (R ? R[i] : L[i])));
        if (a > held) held = a;
        else if (a > held * 0.5) held *= heldDecay;
        const double tg = targetGain();
        inGain += (tg < inGain ? levelAttack : levelRelease) * (tg - inGain);
        const float l = (float) (L[i] * inGain), r = R ? (float) (R[i] * inGain) : l;
        const double g = gate.process (std::max (std::abs (l), std::abs (r)));
        gSum += (float) g;
        double ol, orr;
        if (bassMode) { ol = bass[0].process (l); orr = R ? bass[1].process (r) : ol; }
        else          { ol = guitar[0].process (l); orr = R ? guitar[1].process (r) : ol; }
        L[i] = (float) (ol * g);
        if (R) R[i] = (float) (orr * g);
        peak = std::max (peak, std::abs (L[i]));
    }
    gateMeter.store (n > 0 ? gSum / (float) n : 0.0f);
    if (peak > outMeter.load()) outMeter.store (peak);
}

double AmpProcessor::targetGain() const
{
    const double ref = juce::Decibels::decibelsToGain (isBassMode() ? kRefPeakBassDb : kRefPeakGuitarDb);
    if (held < 1.0e-5) return 1.0; // noch nichts gespielt
    return juce::jlimit (juce::Decibels::decibelsToGain (-12.0), juce::Decibels::decibelsToGain (24.0), ref / held);
}

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("learnedPeak", held, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            held = (double) vt.getProperty ("learnedPeak", 0.0);
            inGain = targetGain();
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* AmpProcessor::createEditor()
{
    return new AmpEditor (*this);
}

#ifndef IRONCURVE_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpProcessor();
}
#endif
