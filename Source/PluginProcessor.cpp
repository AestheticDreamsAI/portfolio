#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ironcurve;

namespace
{
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    juce::String formatHz (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String joinDoubles (const std::vector<double>& v)
    {
        juce::StringArray parts;
        for (double d : v)
            parts.add (juce::String (d, 2));
        return parts.joinIntoString (",");
    }

    std::vector<double> parseDoubles (const juce::String& s)
    {
        std::vector<double> out;
        for (const auto& t : juce::StringArray::fromTokens (s, ",", ""))
            if (t.isNotEmpty())
                out.push_back (t.getDoubleValue());
        return out;
    }

    // Stufe -> Band (HPF und LPF haben je zwei Biquads)
    constexpr int stageBand (int stage)
    {
        return stage <= 1 ? kHpf : (stage >= kNumBands ? kLpf : stage - 1);
    }
}

juce::String IroncurveProcessor::bandName (int band)
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

juce::AudioProcessorValueTreeState::ParameterLayout IroncurveProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

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

    return { params.begin(), params.end() };
}

IroncurveProcessor::IroncurveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& bp = bandParams[(size_t) b];
        bp.on = apvts.getRawParameterValue (bandId (b, "on"));
        bp.freq = apvts.getRawParameterValue (bandId (b, "freq"));
        if (bandHasGain (b))
        {
            bp.gain = apvts.getRawParameterValue (bandId (b, "gain"));
            bp.q = apvts.getRawParameterValue (bandId (b, "q"));
        }
    }
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
            gainSmooth[(size_t) b].setCurrentAndTargetValue (on ? bp.gain->load() * amountParam->load() : 0.0f);
        }
        cutActive[(size_t) b] = on;
    }

    outputSmooth.reset (sampleRate, 0.05);
    outputSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    for (auto& ch : filters)
        for (auto& f : ch)
            f.reset();

    stageActive.fill (false);
    updateFilters (true);
}

void IroncurveProcessor::updateFilters (bool force)
{
    const double fs = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

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

    const double fs = getSampleRate();
    const float amount = amountParam->load();

    // Zielwerte setzen
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
    outputSmooth.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

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

        const float g0 = outputSmooth.getCurrentValue();
        outputSmooth.skip (len);
        const float g1 = outputSmooth.getCurrentValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.applyGainRamp (ch, pos, len, g0, g1);
    }
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
    applyFitResult (result);

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
    FitResult r;
    r.eq = EqSettings::flat();
    applyFitResult (r);
    statusText = juce::String::fromUTF8 ("EQ zurückgesetzt (gelerntes Spektrum bleibt erhalten).");
}

void IroncurveProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void IroncurveProcessor::applyFitResult (const FitResult& r)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& band = r.eq.bands[(size_t) b];
        setParam (bandId (b, "on"), band.enabled ? 1.0f : 0.0f);
        setParam (bandId (b, "freq"), (float) band.freq);
        if (bandHasGain (b))
        {
            setParam (bandId (b, "gain"), (float) band.gainDb);
            setParam (bandId (b, "q"), (float) band.q);
        }
    }
    setParam ("output", (float) r.outputTrimDb);
}

EqSettings IroncurveProcessor::getCurrentEqSettings() const
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

// ---------------------------------------------------------------------------
// State

void IroncurveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto learned = state.getOrCreateChildWithName ("Learned", nullptr);
    {
        const juce::ScopedLock sl (learnedLock);
        learned.setProperty ("spectrum", joinDoubles (learnedDb), nullptr);
        learned.setProperty ("target", joinDoubles (alignedTargetDb), nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void IroncurveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    auto learned = tree.getChildWithName ("Learned");
    {
        const juce::ScopedLock sl (learnedLock);
        learnedDb = parseDoubles (learned.getProperty ("spectrum").toString());
        alignedTargetDb = parseDoubles (learned.getProperty ("target").toString());
        if (learnedDb.size() != (size_t) AnalysisGrid::kNumBands) learnedDb.clear();
        if (alignedTargetDb.size() != (size_t) AnalysisGrid::kNumBands) alignedTargetDb.clear();
    }
    apvts.replaceState (tree);
}

juce::AudioProcessorEditor* IroncurveProcessor::createEditor()
{
    return new IroncurveEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IroncurveProcessor();
}
