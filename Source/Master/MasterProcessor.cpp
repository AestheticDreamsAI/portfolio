#include "MasterProcessor.h"
#include "MasterEditor.h"
#include "../Shared/StateHelpers.h"

using namespace ironcurve;

const std::array<float, 4> MasterProcessor::targetLufs { -14.0f, -11.0f, -9.0f, -7.0f };

namespace
{
    constexpr int kDynFrames = 30;     // 3 s
    constexpr int kLoudFrames = 40;    // 4 s
    constexpr int kVerifyFrames = 30;  // 3 s pro Kontrollrunde
    constexpr int kMaxVerifyRounds = 4;
    constexpr double kSpectrumSeconds = 6.0;

    double loud (double ms) { return -0.691 + 10.0 * std::log10 (std::max (ms, 1.0e-20)); }

    const ModeProfile& mixBusProfile()
    {
        for (const auto& p : modeProfiles())
            if (juce::String (p.name) == "Mix-Bus")
                return p;
        return modeProfiles().front();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterProcessor::createLayout()
{
    using namespace juce;
    eqparams::ParamList params;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto db = AudioParameterFloatAttributes().withLabel ("dB");

    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "target", 1 }, "Ziel-Lautheit",
        StringArray { "Streaming (-14 LUFS)", "Ausgewogen (-11 LUFS)", "Laut (-9 LUFS)", "Metal laut (-7 LUFS)" }, 2));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "eq_amount", 1 }, String::fromUTF8 ("EQ-Stärke"),
        NormalisableRange<float> (0.0f, 1.5f, 0.01f), 1.0f, pct));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "deharsh", 1 }, String::fromUTF8 ("Schärfe-Schutz"),
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f, pct));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dh_freq", 1 }, String::fromUTF8 ("Schärfe-Frequenz"),
        eqparams::logRange (1500.0f, 8000.0f), 3800.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return eqparams::formatHz (v); })));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dh_thr", 1 }, String::fromUTF8 ("Schärfe-Schwelle"),
        NormalisableRange<float> (-40.0f, 0.0f, 0.01f), -14.0f, db));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "mono_on", 1 }, "Mono-Bass", true));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mono_freq", 1 }, "Mono-Bass bis",
        eqparams::logRange (40.0f, 300.0f), 120.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return eqparams::formatHz (v); })));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Breite",
        NormalisableRange<float> (0.0f, 1.5f, 0.01f), 1.0f, pct));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "glue", 1 }, "Glue",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, pct));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp_thr", 1 }, "Kompressor-Schwelle",
        NormalisableRange<float> (-40.0f, 0.0f, 0.01f), -18.0f, db));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
        NormalisableRange<float> (-12.0f, 18.0f, 0.01f), 0.0f, db));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
        NormalisableRange<float> (-3.0f, -0.1f, 0.01f), -1.0f, db));

    eqparams::addBandParameters (params);
    return { params.begin(), params.end() };
}

MasterProcessor::MasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    eqChain.attach (apvts);
    eqAmount = apvts.getRawParameterValue ("eq_amount");
    target = apvts.getRawParameterValue ("target");
    deharshAmt = apvts.getRawParameterValue ("deharsh");
    dhFreq = apvts.getRawParameterValue ("dh_freq");
    dhThr = apvts.getRawParameterValue ("dh_thr");
    monoOn = apvts.getRawParameterValue ("mono_on");
    monoFreq = apvts.getRawParameterValue ("mono_freq");
    width = apvts.getRawParameterValue ("width");
    glue = apvts.getRawParameterValue ("glue");
    compThr = apvts.getRawParameterValue ("comp_thr");
    drive = apvts.getRawParameterValue ("drive");
    ceiling = apvts.getRawParameterValue ("ceiling");

    analyzer.onLearnFinished = [this] (std::vector<double> db) { onSpectrumLearned (std::move (db)); };

    statusText = juce::String::fromUTF8 ("Bereit. Ziel-Lautheit wählen, den Refrain abspielen und LERNEN drücken (ca. 20 s).");
    startTimerHz (20);
}

MasterProcessor::~MasterProcessor()
{
    stopTimer();
}

bool MasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MasterProcessor::prepareToPlay (double sampleRate, int)
{
    analyzer.prepare (sampleRate);
    eqChain.prepare (sampleRate, eqAmount->load());
    deharsher.prepare (sampleRate);
    shaper.prepare (sampleRate);
    comp.prepare (sampleRate);
    limiter.prepare (sampleRate, 2);
    kw.prepare (sampleRate);
    for (auto& k : kPre) k.setup (kw);
    for (auto& k : kPost) k.setup (kw);

    lastDhFreq = lastMonoFreq = lastMonoOn = -1.0f;
    driveSmooth.reset (sampleRate, 0.2);
    driveSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (drive->load()));

    frameLen = juce::jmax (1, (int) std::round (sampleRate * 0.1));
    acc = {};
    accCount = 0;

    setLatencySamples (limiter.getLatencySamples());
}

void MasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const int numCh = juce::jmin (2, numIn, buffer.getNumChannels());
    if (numCh <= 0)
        return;
    const bool stereo = numCh == 2;

    analyzer.pushAudio (buffer, numCh, numSamples);
    eqChain.process (buffer, numCh, eqAmount->load());

    // Parameter fuer diesen Block
    if (dhFreq->load() != lastDhFreq)
    {
        lastDhFreq = dhFreq->load();
        deharsher.setFrequency (lastDhFreq);
    }
    deharsher.setParams (dhThr->load(), 10.0 * deharshAmt->load());

    if (monoFreq->load() != lastMonoFreq || monoOn->load() != lastMonoOn)
    {
        lastMonoFreq = monoFreq->load();
        lastMonoOn = monoOn->load();
        shaper.setMonoBelow (lastMonoFreq, lastMonoOn > 0.5f);
    }
    shaper.setWidth (width->load());
    comp.setParams (compThr->load(), 1.0 + 3.0 * glue->load());
    limiter.setCeilingDb (ceiling->load());
    driveSmooth.setTargetValue (juce::Decibels::decibelsToGain (drive->load()));

    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;
    float maxDh = 0, maxComp = 0, maxLim = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = L[i];
        float r = stereo ? R[i] : l;

        deharsher.process (l, r);
        if (stereo)
            shaper.process (l, r);

        const float g = (float) comp.process (l, r);
        l *= g;
        r *= g;

        // Lautheit vor Drive (fuer die Drive-Berechnung)
        const float pl = kPre[0].process (l);
        const float pr = stereo ? kPre[1].process (r) : 0.0f;
        acc.msPre += (double) pl * pl + (double) pr * pr;
        acc.compLevelDb += comp.lastLevelDb();
        acc.harshDb += deharsher.lastRatioDb();

        const float d = driveSmooth.getNextValue();
        l *= d;
        r *= d;
        limiter.process (l, r);

        const float ql = kPost[0].process (l);
        const float qr = stereo ? kPost[1].process (r) : 0.0f;
        acc.msPost += (double) ql * ql + (double) qr * qr;

        maxDh = std::max (maxDh, (float) deharsher.currentGainReductionDb());
        maxComp = std::max (maxComp, (float) comp.currentGainReductionDb());
        maxLim = std::max (maxLim, (float) limiter.currentGainReductionDb());

        L[i] = l;
        if (stereo) R[i] = r;

        if (++accCount >= frameLen)
        {
            int s1, n1, s2, n2;
            frameFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0)
            {
                auto& f = frameBuf[(size_t) s1];
                f.msPre = acc.msPre / accCount;
                f.msPost = acc.msPost / accCount;
                f.compLevelDb = acc.compLevelDb / accCount;
                f.harshDb = acc.harshDb / accCount;
                frameFifo.finishedWrite (1);
            }
            acc = {};
            accCount = 0;
        }
    }

    // Meter: Maximum seit dem letzten Abholen
    auto raise = [] (std::atomic<float>& a, float v) { if (v > a.load()) a.store (v); };
    raise (grDeharsh, maxDh);
    raise (grComp, maxComp);
    raise (grLimit, maxLim);
}

// ---------------------------------------------------------------------------
// Lernen

float MasterProcessor::getTargetLufs() const
{
    return targetLufs[(size_t) juce::jlimit (0, 3, (int) target->load())];
}

void MasterProcessor::startLearning()
{
    enterPhase (Phase::Spectrum);
}

void MasterProcessor::cancelLearning()
{
    analyzer.cancelLearning();
    phase = Phase::Idle;
    statusText = "Lernen abgebrochen.";
}

void MasterProcessor::enterPhase (Phase p)
{
    phase = p;
    phaseData.clear();
    phaseFrames = 0;
    limitGrSum = 0.0;
    skipFrames = 4; // Parameter-Glaettung abwarten

    switch (p)
    {
        case Phase::Spectrum:
            analyzer.startLearning (kSpectrumSeconds);
            statusText = juce::String::fromUTF8 ("1/4 Klangbalance: höre zu … spiel den Refrain oder den lautesten Teil.");
            break;
        case Phase::Dynamics:
            statusText = juce::String::fromUTF8 ("2/4 Dynamik: messe Pegel und Schärfe …");
            break;
        case Phase::Loudness:
            statusText = juce::String::fromUTF8 ("3/4 Lautheit: stelle den Drive auf das Ziel ein …");
            break;
        case Phase::Verify:
            statusText = juce::String::fromUTF8 ("4/4 Kontrolle: messe das Ergebnis nach dem Limiter …");
            break;
        case Phase::Idle:
            break;
    }
}

void MasterProcessor::onSpectrumLearned (std::vector<double> db)
{
    const auto result = fitEq (mixBusProfile(), db, getAnalysisRate());

    // Frequenz fuer den Schaerfe-Schutz: groesster Ueberschuss gegenueber dem Ziel zwischen 2 und 6 kHz
    const auto smooth = smoothDb (db, 4);
    double bestF = 3800.0, best = -1.0e9;
    for (int i = 0; i < AnalysisGrid::kNumBands; ++i)
    {
        const double f = AnalysisGrid::centerHz (i);
        if (f < 2000.0 || f > 6000.0)
            continue;
        const double ex = smooth[(size_t) i] - result.alignedTargetDb[(size_t) i];
        if (ex > best) { best = ex; bestF = f; }
    }

    {
        const juce::ScopedLock sl (learnedLock);
        learnedDb = std::move (db);
        alignedTargetDb = result.alignedTargetDb;
    }

    eqparams::applyEq (apvts, result.eq);
    setParam ("dh_freq", (float) bestF);
    enterPhase (Phase::Dynamics);
}

void MasterProcessor::timerCallback()
{
    // Meter
    meters.deharshGr = grDeharsh.exchange (0.0f);
    meters.compGr = grComp.exchange (0.0f);
    meters.limitGr = grLimit.exchange (0.0f);

    int s1, n1, s2, n2;
    const int ready = frameFifo.getNumReady();
    frameFifo.prepareToRead (ready, s1, n1, s2, n2);
    std::vector<Frame> frames;
    for (int i = 0; i < n1; ++i) frames.push_back (frameBuf[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) frames.push_back (frameBuf[(size_t) (s2 + i)]);
    frameFifo.finishedRead (n1 + n2);

    for (const auto& f : frames)
    {
        shortTerm.push_back (f.msPost);
        while (shortTerm.size() > 30)
            shortTerm.pop_front();

        if (phase == Phase::Idle || phase == Phase::Spectrum)
            continue;
        if (skipFrames > 0) { --skipFrames; continue; }
        if (loud (f.msPre) < -45.0)
            continue; // Stille / Pausen zaehlen nicht

        phaseData.push_back (f);
        ++phaseFrames;
        if (phase == Phase::Verify)
            limitGrSum += meters.limitGr;

        if (phase == Phase::Dynamics && phaseFrames >= kDynFrames)
        {
            double lvl = 0, harsh = 0;
            for (const auto& d : phaseData) { lvl += d.compLevelDb; harsh += d.harshDb; }
            lvl /= phaseData.size();
            harsh /= phaseData.size();
            setParam ("comp_thr", (float) juce::jlimit (-40.0, 0.0, lvl + 0.5));
            setParam ("dh_thr", (float) juce::jlimit (-40.0, 0.0, harsh + 2.0));
            enterPhase (Phase::Loudness);
        }
        else if (phase == Phase::Loudness && phaseFrames >= kLoudFrames)
        {
            std::vector<double> ms;
            for (const auto& d : phaseData) ms.push_back (d.msPre);
            const double l = gatedLoudness (ms);
            setParam ("drive", (float) juce::jlimit (-12.0, 18.0, getTargetLufs() - l));
            enterPhase (Phase::Verify);
            verifyRound = 0;
        }
        else if (phase == Phase::Verify && phaseFrames >= kVerifyFrames)
        {
            std::vector<double> ms;
            for (const auto& d : phaseData) ms.push_back (d.msPost);
            const double l = gatedLoudness (ms);
            const double diff = getTargetLufs() - l;
            const double avgLim = limitGrSum / phaseFrames;
            const bool atMax = drive->load() >= 17.9f;
            ++verifyRound;

            if (std::abs (diff) <= 0.5 || verifyRound >= kMaxVerifyRounds || (atMax && diff > 0))
            {
                phase = Phase::Idle;
                juce::String s = juce::String::fromUTF8 ("Fertig! Gemessen ") + juce::String (l, 1) + " LUFS (Ziel "
                               + juce::String (getTargetLufs(), 0) + juce::String::fromUTF8 ("), Schärfe-Schutz bei ")
                               + eqparams::formatHz (dhFreq->load()) + ".";
                if (std::abs (diff) > 0.5)
                    s << juce::String::fromUTF8 (" Ziel nicht ganz erreicht, der Mix lässt sich ohne Verzerrung kaum lauter machen.");
                else if (avgLim > 4.0)
                    s << juce::String::fromUTF8 (" Achtung: Limiter arbeitet hart. Eine leisere Ziel-Lautheit klingt evtl. besser.");
                statusText = s;
            }
            else
            {
                // Der Limiter frisst bei mehr Drive einen Teil der Lautheit: nach oben etwas mehr korrigieren
                const double step = diff > 0 ? diff * 1.4 : diff;
                setParam ("drive", (float) juce::jlimit (-12.0, 18.0, (double) drive->load() + step));
                const int round = verifyRound;
                enterPhase (Phase::Verify);
                verifyRound = round;
            }
        }
    }

    // Short-Term-Lautheit (3 s, ungated)
    if (! shortTerm.empty())
    {
        double sum = 0.0;
        for (double m : shortTerm) sum += m;
        meters.shortTermLufs = (float) loud (sum / shortTerm.size());
    }
}

float MasterProcessor::getLearnProgress() const
{
    switch (phase)
    {
        case Phase::Spectrum: return 0.35f * analyzer.getLearnProgress();
        case Phase::Dynamics: return 0.35f + 0.15f * (float) phaseFrames / kDynFrames;
        case Phase::Loudness: return 0.50f + 0.25f * (float) phaseFrames / kLoudFrames;
        case Phase::Verify:   return juce::jmin (0.99f, 0.75f + 0.25f * ((float) verifyRound + (float) phaseFrames / kVerifyFrames) / 2.0f);
        case Phase::Idle:     break;
    }
    return 0.0f;
}

juce::String MasterProcessor::getPhaseName() const
{
    switch (phase)
    {
        case Phase::Spectrum: return "Klang";
        case Phase::Dynamics: return "Dynamik";
        case Phase::Loudness: return "Lautheit";
        case Phase::Verify:   return "Kontrolle";
        case Phase::Idle:     break;
    }
    return {};
}

void MasterProcessor::resetAll()
{
    eqparams::applyEq (apvts, EqSettings::flat());
    setParam ("drive", 0.0f);
    setParam ("comp_thr", -18.0f);
    setParam ("dh_thr", -14.0f);
    setParam ("dh_freq", 3800.0f);
    statusText = juce::String::fromUTF8 ("Zurückgesetzt. Drücke LERNEN, um neu einzustellen.");
}

bool MasterProcessor::hasLearnedSpectrum() const
{
    const juce::ScopedLock sl (learnedLock);
    return ! learnedDb.empty();
}

std::vector<double> MasterProcessor::getLearnedSpectrum() const
{
    const juce::ScopedLock sl (learnedLock);
    return learnedDb;
}

std::vector<double> MasterProcessor::getAlignedTarget() const
{
    const juce::ScopedLock sl (learnedLock);
    return alignedTargetDb;
}

void MasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (learnedLock);
    statehelpers::save (*this, apvts, destData, learnedDb, alignedTargetDb);
}

void MasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::vector<double> l, t;
    if (statehelpers::load (*this, apvts, data, sizeInBytes, l, t))
    {
        const juce::ScopedLock sl (learnedLock);
        learnedDb = std::move (l);
        alignedTargetDb = std::move (t);
    }
}

juce::AudioProcessorEditor* MasterProcessor::createEditor()
{
    return new MasterEditor (*this);
}

#ifndef IRONCURVE_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterProcessor();
}
#endif
