#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Shared/StateHelpers.h"

using namespace ironcurve;

juce::AudioProcessorValueTreeState::ParameterLayout IroncurveProcessor::createLayout()
{
    using namespace juce;
    eqparams::ParamList params;

    StringArray modes;
    for (const auto& p : modeProfiles())
        modes.add (String::fromUTF8 (p.name));
    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Modus", modes, 0));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "amount", 1 }, String::fromUTF8 ("Stärke"), NormalisableRange<float> (0.0f, 1.5f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Ausgang", NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    eqparams::addBandParameters (params);
    return { params.begin(), params.end() };
}

IroncurveProcessor::IroncurveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    eqChain.attach (apvts);
    amountParam = apvts.getRawParameterValue ("amount");
    outputParam = apvts.getRawParameterValue ("output");
    modeParam = apvts.getRawParameterValue ("mode");

    analyzer.onLearnFinished = [this] (std::vector<double> db)
    {
        {
            const juce::ScopedLock sl (learnedLock);
            learnedDb = std::move (db);
        }
        refit();
    };

    statusText = juce::String::fromUTF8 ("Bereit. Modus wählen, Song abspielen und LERNEN drücken.");
}

bool IroncurveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void IroncurveProcessor::prepareToPlay (double sampleRate, int)
{
    analyzer.prepare (sampleRate);
    eqChain.prepare (sampleRate, amountParam->load());
    outputSmooth.reset (sampleRate, 0.05);
    outputSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
}

void IroncurveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const int numCh = juce::jmin (2, numIn, buffer.getNumChannels());
    analyzer.pushAudio (buffer, numCh, numSamples); // Analyse vor dem EQ

    eqChain.process (buffer, numCh, amountParam->load());

    outputSmooth.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    const float g0 = outputSmooth.getCurrentValue();
    outputSmooth.skip (numSamples);
    const float g1 = outputSmooth.getCurrentValue();
    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGainRamp (ch, 0, numSamples, g0, g1);
}

// ---------------------------------------------------------------------------
// Auto-EQ

void IroncurveProcessor::startLearning()
{
    analyzer.startLearning (learnSeconds);
    statusText = juce::String::fromUTF8 ("Höre zu … spiel den lautesten, typischsten Part ab (Signal unter -50 dBFS zählt nicht).");
}

void IroncurveProcessor::cancelLearning()
{
    analyzer.cancelLearning();
    statusText = "Lernen abgebrochen.";
}

bool IroncurveProcessor::hasLearnedSpectrum() const
{
    const juce::ScopedLock sl (learnedLock);
    return ! learnedDb.empty();
}

std::vector<double> IroncurveProcessor::getLearnedSpectrum() const
{
    const juce::ScopedLock sl (learnedLock);
    return learnedDb;
}

std::vector<double> IroncurveProcessor::getAlignedTarget() const
{
    const juce::ScopedLock sl (learnedLock);
    return alignedTargetDb;
}

void IroncurveProcessor::refit()
{
    std::vector<double> db = getLearnedSpectrum();
    if (db.size() != (size_t) AnalysisGrid::kNumBands)
    {
        statusText = "Noch nichts gelernt. Erst LERNEN, dann NEU BERECHNEN.";
        return;
    }

    const auto& profiles = modeProfiles();
    const int mode = juce::jlimit (0, (int) profiles.size() - 1, (int) modeParam->load());
    const auto result = fitEq (profiles[(size_t) mode], db, getAnalysisRate());

    {
        const juce::ScopedLock sl (learnedLock);
        alignedTargetDb = result.alignedTargetDb;
    }
    eqparams::applyEq (apvts, result.eq);
    eqparams::setParam (apvts, "output", (float) result.outputTrimDb);

    int active = 0;
    for (const auto& b : result.eq.bands)
        active += b.enabled ? 1 : 0;

    statusText = juce::String::fromUTF8 (profiles[(size_t) mode].name) + ": "
               + juce::String (active) + juce::String::fromUTF8 (" Bänder gesetzt, Pegelausgleich ")
               + (result.outputTrimDb >= 0 ? "+" : "") + juce::String (result.outputTrimDb, 1)
               + juce::String::fromUTF8 (" dB. Nodes ziehen zum Feintunen.");
}

void IroncurveProcessor::resetEq()
{
    eqparams::applyEq (apvts, EqSettings::flat());
    eqparams::setParam (apvts, "output", 0.0f);
    statusText = juce::String::fromUTF8 ("EQ zurückgesetzt (gelerntes Spektrum bleibt erhalten).");
}

// ---------------------------------------------------------------------------
// State

void IroncurveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (learnedLock);
    statehelpers::save (*this, apvts, destData, learnedDb, alignedTargetDb);
}

void IroncurveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::vector<double> l, t;
    if (statehelpers::load (*this, apvts, data, sizeInBytes, l, t))
    {
        const juce::ScopedLock sl (learnedLock);
        learnedDb = std::move (l);
        alignedTargetDb = std::move (t);
    }
}

juce::AudioProcessorEditor* IroncurveProcessor::createEditor()
{
    return new IroncurveEditor (*this);
}

#ifndef IRONCURVE_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IroncurveProcessor();
}
#endif
