#include "AmpProcessor.h"
#include "AmpEditor.h"

using namespace ironcurve;

juce::AudioProcessorValueTreeState::ParameterLayout AmpProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 10.0f, 1); }); // 0..10 wie am Amp
    auto db = AudioParameterFloatAttributes().withLabel ("dB");
    auto knob = [&] (const char* id, const String& name, float def)
    { p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f, 0.001f), def, pct)); };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Modus", StringArray { "Gitarre", "Bass" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Eingang", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    knob ("gate", "Gate", 0.5f);
    knob ("tight", "Tight", 0.6f);
    knob ("gain", "Gain", 0.65f);
    knob ("bass", "Bass", 0.5f);
    knob ("mid", "Mitten", 0.5f);
    knob ("treble", String::fromUTF8 ("Höhen"), 0.5f);
    knob ("presence", "Presence", 0.5f);
    knob ("depth", "Depth", 0.5f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, "Ausgang", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "cab", 1 }, "Box", true));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "xover", 1 }, "Weiche",
        NormalisableRange<float> (80.0f, 400.0f, 1.0f, 0.5f), 200.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lowlevel", 1 }, "Tiefband", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f, db));
    knob ("grit", "Grit", 0.6f);
    return { p.begin(), p.end() };
}

AmpProcessor::AmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    auto r = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    mode = r ("mode"); input = r ("input"); gateAmt = r ("gate"); tight = r ("tight"); gain = r ("gain");
    bassP = r ("bass"); mid = r ("mid"); treble = r ("treble"); presence = r ("presence"); depth = r ("depth");
    level = r ("level"); cab = r ("cab"); xover = r ("xover"); lowLevel = r ("lowlevel"); grit = r ("grit");
}

bool AmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

AmpParams AmpProcessor::readParams() const
{
    AmpParams p;
    p.inputDb = input->load();
    p.gate = gateAmt->load();
    p.tight = tight->load();
    p.gain = gain->load();
    p.bass = bassP->load(); p.mid = mid->load(); p.treble = treble->load();
    p.presence = presence->load(); p.depth = depth->load();
    p.levelDb = level->load();
    p.cab = cab->load() > 0.5f;
    p.crossoverHz = xover->load();
    p.lowDb = lowLevel->load();
    p.grit = grit->load();
    return p;
}

void AmpProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& g : guitar) g.prepare (sampleRate);
    for (auto& b : bass) b.prepare (sampleRate);
    gate.prepare (sampleRate);
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
        const float l = L[i], r = R ? R[i] : l;
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

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
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
