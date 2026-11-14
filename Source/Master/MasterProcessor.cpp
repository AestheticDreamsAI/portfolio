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
    constexpr int kMaxVerifyRounds = 7;
    constexpr double kMaxCleanGr = 3.0;   // mehr mittlere Begrenzung klingt gequetscht
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
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "clarity", 1 }, "Klarheit",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, pct));
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
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "clip", 1 }, "Clipper", true));
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
    clarityAmt = apvts.getRawParameterValue ("clarity");
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
    clipOn = apvts.getRawParameterValue ("clip");

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
    clarity.prepare (sampleRate);
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

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) kChunk);
    oversampler->reset();
    work.setSize (2, kChunk);
    preE.assign ((size_t) kChunk, 0.0);
    preLvl.assign ((size_t) kChunk, 0.0);
    preHarsh.assign ((size_t) kChunk, 0.0);

    setLatencySamples (clarity.getLatencySamples() + limiter.getLatencySamples()
                       + (int) std::round (oversampler->getLatencyInSamples()));
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
    const bool ref = refMode.load();
    if (dhFreq->load() != lastDhFreq)
    {
        lastDhFreq = dhFreq->load();
        deharsher.setFrequency (lastDhFreq);
    }
    deharsher.setParams (dhThr->load(), 10.0 * deharshAmt->load());
    clarity.setAmount (clarityAmt->load());

    // Im Referenz-Modus: Mono-Bass genau dann, wenn die Referenz ihn hat; Breite unveraendert
    const float wantMonoFreq = ref ? 120.0f : monoFreq->load();
    const float wantMonoOn = ref ? (refMonoBass.load() ? 1.0f : 0.0f) : monoOn->load();
    if (wantMonoFreq != lastMonoFreq || wantMonoOn != lastMonoOn)
    {
        lastMonoFreq = wantMonoFreq;
        lastMonoOn = wantMonoOn;
        shaper.setMonoBelow (lastMonoFreq, lastMonoOn > 0.5f);
    }
    shaper.setWidth (ref ? 1.0 : width->load());
    comp.setParams (compThr->load(), 1.0 + 3.0 * glue->load());
    limiter.setCeilingDb (ceiling->load());
    const float clipT = juce::Decibels::decibelsToGain (ceiling->load());
    driveSmooth.setTargetValue (juce::Decibels::decibelsToGain (drive->load()));

    float maxDh = 0, maxComp = 0, maxLim = 0, maxClip = 0;

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = juce::jmin (kChunk, numSamples - pos);
        float* sl = work.getWritePointer (0);
        float* sr = work.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            sl[i] = buffer.getSample (0, pos + i);
            sr[i] = stereo ? buffer.getSample (1, pos + i) : sl[i];
        }

        // --- Stufe 1: Klarheit, Schaerfe-Schutz, Stereo, Glue, Drive -----------------
        for (int i = 0; i < n; ++i)
        {
            float l = sl[i], r = sr[i];

            clarity.process (l, r); // in beiden Modi aktiv
            if (! ref)
                deharsher.process (l, r);
            if (stereo)
                shaper.process (l, r);

            const float g = (float) comp.process (l, r);
            l *= g;
            r *= g;

            // Lautheit vor Drive (fuer die Drive-Berechnung)
            const float pl = kPre[0].process (l);
            const float pr = stereo ? kPre[1].process (r) : 0.0f;
            preE[(size_t) i] = (double) pl * pl + (double) pr * pr;
            preLvl[(size_t) i] = comp.lastLevelDb();
            preHarsh[(size_t) i] = deharsher.lastRatioDb();

            if (! ref)
                maxDh = std::max (maxDh, (float) deharsher.currentGainReductionDb());
            maxComp = std::max (maxComp, (float) comp.currentGainReductionDb());

            const float d = driveSmooth.getNextValue();
            sl[i] = l * d;
            sr[i] = r * d;
        }

        // --- Stufe 2: Soft-Clipper mit 4-facher Ueberabtastung ------------------------
        // Schneidet Spitzen weich ab, bevor der Limiter greifen muss: mehr Dichte und Lautheit
        // bei weniger Pumpen (so arbeiten auch moderne Metal-Master).
        if (clipOn->load() > 0.5f)
        {
            juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 2, (size_t) n);
            auto up = oversampler->processSamplesUp (block);
            const float a = 0.6f * clipT, span = clipT - a;
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                float* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const float x = d[i], ax = std::abs (x);
                    if (ax > a)
                    {
                        const float y = a + span * std::tanh ((ax - a) / span);
                        maxClip = std::max (maxClip, ax / y);
                        d[i] = std::copysign (y, x);
                    }
                }
            }
            oversampler->processSamplesDown (block);
        }

        // --- Stufe 3: True-Peak-Limiter, Messung ---------------------------------------
        for (int i = 0; i < n; ++i)
        {
            float l = sl[i], r = sr[i];
            limiter.process (l, r);

            const float ql = kPost[0].process (l);
            const float qr = stereo ? kPost[1].process (r) : 0.0f;
            acc.msPre += preE[(size_t) i];
            acc.msPost += (double) ql * ql + (double) qr * qr;
            acc.compLevelDb += preLvl[(size_t) i];
            acc.harshDb += preHarsh[(size_t) i];
            maxLim = std::max (maxLim, (float) limiter.currentGainReductionDb());

            buffer.setSample (0, pos + i, l);
            if (stereo) buffer.setSample (1, pos + i, r);

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
    }

    // Meter: Maximum seit dem letzten Abholen (Clipper zaehlt zum Limiter-Balken)
    auto raise = [] (std::atomic<float>& a, float v) { if (v > a.load()) a.store (v); };
    raise (grDeharsh, maxDh);
    raise (grComp, maxComp);
    raise (grLimit, maxLim + (maxClip > 1.0f ? juce::Decibels::gainToDecibels (maxClip) : 0.0f));
}

// ---------------------------------------------------------------------------
// Lernen

float MasterProcessor::getTargetLufs() const
{
    if (refMode.load())
        return (float) referenceLufs;
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
    if (refMode.load())
    {
        std::vector<double> ref;
        {
            const juce::ScopedLock sl (learnedLock);
            ref = referenceDb;
        }
        const auto result = fitEq (makeReferenceProfile (ref), db, getAnalysisRate());
        {
            const juce::ScopedLock sl (learnedLock);
            learnedDb = std::move (db);
            alignedTargetDb = result.alignedTargetDb;
        }
        eqparams::applyEq (apvts, result.eq);
        enterPhase (Phase::Dynamics); // Kompressor-Schwelle fuer die Dichte der Referenz
        return;
    }

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
            loudnessCapped = false;
        }
        else if (phase == Phase::Verify && phaseFrames >= kVerifyFrames)
        {
            std::vector<double> ms;
            for (const auto& d : phaseData) ms.push_back (d.msPost);
            const double l = gatedLoudness (ms);
            const double diff = getTargetLufs() - l;
            const double avgLim = limitGrSum / phaseFrames;   // wie hart Clipper + Limiter im Schnitt arbeiten
            const bool atMax = drive->load() >= 17.9f;
            ++verifyRound;

            // Sauberkeits-Grenze: Muesste der Limiter im Schnitt mehr als kMaxCleanGr arbeiten,
            // wird der Drive zurueckgenommen statt dem Ziel hinterherzujagen.
            const bool tooHard = avgLim > kMaxCleanGr + 0.5;
            if (tooHard || (loudnessCapped && diff > 0.0))
                loudnessCapped = true;

            const bool reached = std::abs (diff) <= 0.5;
            const bool cappedOk = loudnessCapped && ! tooHard;
            const bool done = (reached && ! tooHard) || cappedOk || verifyRound >= kMaxVerifyRounds || (atMax && diff > 0 && ! tooHard);

            if (done)
            {
                phase = Phase::Idle;
                juce::String s;
                if (refMode.load())
                    s = juce::String::fromUTF8 ("Fertig! Klang wie „") + referenceName + juce::String::fromUTF8 ("“, ");
                else
                    s = juce::String::fromUTF8 ("Fertig! Schärfe-Schutz bei ") + eqparams::formatHz (dhFreq->load()) + ", ";

                if (loudnessCapped && diff > 0.5)
                    s << juce::String::fromUTF8 ("Lautheit auf saubere ") << juce::String (l, 1)
                      << juce::String::fromUTF8 (" LUFS begrenzt (Ziel ") << juce::String (getTargetLufs(), 1)
                      << juce::String::fromUTF8 (" LUFS). Lauter ginge nur mit hörbarem Quetschen und Verzerren.");
                else if (diff > 0.5)
                    s << juce::String (l, 1) << juce::String::fromUTF8 (" LUFS, Ziel nicht ganz erreicht.");
                else
                    s << juce::String (l, 1) << " LUFS (Ziel " << juce::String (getTargetLufs(), 1) << ").";
                statusText = s;
            }
            else
            {
                double step;
                if (tooHard)
                    step = -(avgLim - kMaxCleanGr);           // zu hart: so viel zuruecknehmen, wie zu viel begrenzt wird
                else
                    step = diff > 0 ? diff * 1.4 : diff;      // Limiter frisst bei mehr Drive einen Teil der Lautheit
                if (loudnessCapped && step > 0)
                    step = 0.0;                               // nach Begrenzung nie wieder hoch
                setParam ("drive", (float) juce::jlimit (-12.0, 18.0, (double) drive->load() + step));
                const int round = verifyRound;
                const bool capped = loudnessCapped;
                enterPhase (Phase::Verify);
                verifyRound = round;
                loudnessCapped = capped;
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

bool MasterProcessor::loadReference (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        statusText = juce::String::fromUTF8 ("Konnte die Datei nicht lesen. Bitte eine WAV-, AIFF- oder FLAC-Datei wählen.");
        return false;
    }

    auto spectrum = SpectrumAnalyzer::analyzeFile (*reader);

    // Integrierte Lautheit nach BS.1770 (100-ms-Frames, K-gewichtet, gegated)
    double lufs = -100.0, bassSideDb = 0.0;
    {
        const double fs = reader->sampleRate;
        const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
        KWeighting k; k.prepare (fs);
        KWeightedChannel ch[2]; ch[0].setup (k); ch[1].setup (k);
        Biquad lpM[2], lpS[2];
        for (int q = 0; q < 2; ++q)
        {
            lpM[q].c = makeCoeffs (FilterType::LowPass, 100.0, q == 0 ? kButterQ1 : kButterQ2, 0.0, fs);
            lpS[q].c = lpM[q].c;
        }
        double eMid = 0.0, eSide = 0.0;
        const int frame = juce::jmax (1, (int) (fs * 0.1));
        juce::AudioBuffer<float> buf (numCh, frame);
        std::vector<double> frames;
        for (juce::int64 pos = 0; pos + frame <= reader->lengthInSamples; pos += frame)
        {
            reader->read (&buf, 0, frame, pos, true, numCh > 1);
            double ms = 0.0;
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < frame; ++i)
                {
                    const float y = ch[c].process (buf.getSample (c, i));
                    ms += (double) y * y;
                }
            if (numCh == 2)
                for (int i = 0; i < frame; ++i)
                {
                    const float l = buf.getSample (0, i), r = buf.getSample (1, i);
                    const float m = lpM[1].process (lpM[0].process (0.5f * (l + r)));
                    const float sd = lpS[1].process (lpS[0].process (0.5f * (l - r)));
                    eMid += (double) m * m;
                    eSide += (double) sd * sd;
                }
            frames.push_back (ms / frame);
        }
        // Lautester 20-s-Abschnitt (beim Lernen spielt man ja auch den lauten Teil)
        if (frames.size() <= 200)
            lufs = gatedLoudness (frames);
        else
            for (size_t st = 0; st + 200 <= frames.size(); st += 10)
                lufs = std::max (lufs, gatedLoudness (std::vector<double> (frames.begin() + (long) st, frames.begin() + (long) st + 200)));

        // Ist der Bass der Referenz mono? (Seite gegenueber Mitte unter 100 Hz)
        bassSideDb = 10.0 * std::log10 ((eSide + 1.0e-20) / (eMid + 1.0e-20));
    }

    if (spectrum.empty() || lufs < -60.0)
    {
        statusText = juce::String::fromUTF8 ("In der Datei ist kaum etwas Hörbares. Bitte eine andere Version wählen.");
        return false;
    }

    {
        const juce::ScopedLock sl (learnedLock);
        referenceDb = std::move (spectrum);
    }
    referenceLufs = lufs;
    referenceName = file.getFileNameWithoutExtension();
    refMonoBass = bassSideDb < -14.0;
    refMode = true;

    // Lernen startet automatisch: kein Extra-Klick, keine Gefahr, mit alten Einstellungen weiterzuarbeiten
    startLearning();
    statusText = juce::String::fromUTF8 ("Referenz „") + referenceName + "“ (" + juce::String (lufs, 1)
               + juce::String::fromUTF8 (" LUFS) geladen – lerne jetzt automatisch. Spiel den Refrain ab!");
    return true;
}

void MasterProcessor::clearReference()
{
    {
        const juce::ScopedLock sl (learnedLock);
        referenceDb.clear();
    }
    referenceName.clear();
    referenceLufs = -100.0;
    refMode = false;
    refMonoBass = false;
    statusText = juce::String::fromUTF8 ("Referenz entfernt. Automatik-Modus: LERNEN drücken.");
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
    auto state = apvts.copyState();
    auto node = state.getOrCreateChildWithName ("Learned", nullptr);
    node.setProperty ("spectrum", statehelpers::joinDoubles (learnedDb), nullptr);
    node.setProperty ("target", statehelpers::joinDoubles (alignedTargetDb), nullptr);
    node.setProperty ("reference", statehelpers::joinDoubles (referenceDb), nullptr);
    node.setProperty ("referenceName", referenceName, nullptr);
    node.setProperty ("referenceLufs", referenceLufs, nullptr);
    node.setProperty ("referenceMonoBass", refMonoBass.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto node = tree.getChildWithName ("Learned");
    {
        const juce::ScopedLock sl (learnedLock);
        learnedDb = statehelpers::parseDoubles (node.getProperty ("spectrum").toString());
        alignedTargetDb = statehelpers::parseDoubles (node.getProperty ("target").toString());
        referenceDb = statehelpers::parseDoubles (node.getProperty ("reference").toString());
        refMode = ! referenceDb.empty();
    }
    referenceName = node.getProperty ("referenceName").toString();
    referenceLufs = (double) node.getProperty ("referenceLufs", -100.0);
    refMonoBass = (bool) node.getProperty ("referenceMonoBass", false);
    apvts.replaceState (tree);
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
