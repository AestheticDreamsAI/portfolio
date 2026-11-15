#include "AmpProcessor.h"
#include "CabCurve.h"
#include "AmpEditor.h"

using namespace ironcurve;

juce::AudioProcessorValueTreeState::ParameterLayout AmpProcessor::createLayout()
{
    using namespace juce;
    StringArray names;
    for (auto& pr : ampPresets()) names.add (String::fromUTF8 (pr.name));
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "preset", 1 }, "Preset", names, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "stereo", 1 }, "Mono/Stereo", StringArray { "Mono", "Stereo" }, 0));
    return { p.begin(), p.end() };
}

AmpProcessor::AmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    preset = apvts.getRawParameterValue ("preset");
    stereo = apvts.getRawParameterValue ("stereo");
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

void AmpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    for (auto& g : guitar) g.prepare (sampleRate);
    for (auto& b : bass) b.prepare (sampleRate);
    for (auto& gt : gates) gt.prepare (sampleRate);
    midEnergy = sideEnergy = 0.0;
    levelAttack = 1.0 - std::exp (-1.0 / (0.002 * sampleRate));
    levelRelease = 1.0 - std::exp (-1.0 / (0.4 * sampleRate));
    heldDecay = std::pow (10.0, -0.5 / 20.0 / sampleRate); // 0,5 dB/s
    if (inGain <= 0.0) inGain = targetGain();
    delayLine.assign ((size_t) (0.05 * sampleRate) + 8, 0.0f);
    delayPos = 0;
    auto p = readParams();
    if (forceNoCab) p.cab = false;
    for (auto& g : guitar) g.setParams (p);
    for (auto& b : bass) b.setParams (p);
    lastBass = isBassMode();
    cabConv.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 });
    cabConv.loadImpulseResponse (makeCabIR (sampleRate), sampleRate, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    cabConv.reset();
    setLatencySamples (Oversampler4x::latency() + cabConv.getLatency());
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
    auto p = readParams();
    if (forceNoCab) p.cab = false;
    if (bassMode != lastBass) // Moduswechsel: Zustand zuruecksetzen, damit nichts knallt
    {
        for (auto& g : guitar) g.prepare (getSampleRate());
        for (auto& b : bass) b.prepare (getSampleRate());
        lastBass = bassMode;
    }
    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    // Ist der Eingang Mono (eine Gitarre, mittig) oder echtes Stereo (z. B. zwei Aufnahmen links/rechts
    // oder vorher gepannt)? Echtes Stereo wird Seite fuer Seite verarbeitet, damit Panning erhalten bleibt.
    if (R != nullptr)
    {
        double mE = 0.0, sE = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * (L[i] + R[i]), d = 0.5 * (L[i] - R[i]);
            mE += m * m; sE += d * d;
        }
        if (mE + sE > 1.0e-9 * n) // nur entscheiden, wenn etwas gespielt wird
        {
            const double c = 1.0 - std::exp (-(double) n / (0.3 * getSampleRate()));
            midEnergy += c * (mE / n - midEnergy);
            sideEnergy += c * (sE / n - sideEnergy);
        }
    }
    const bool monoInput = R == nullptr || sideEnergy < midEnergy * 0.001; // Seiten < -30 dB
    const bool wide = isStereo() && monoInput; // Doppeln nur, wenn es nicht schon Stereo ist

    // Stereo-Doppeln: zweite Seite leicht anders "gespielt" (andere Klangfarbe + kurze Verzoegerung)
    auto p2 = p;
    if (wide)
    {
        p2.gain = juce::jlimit (0.0, 1.0, p.gain + 0.04);
        p2.presence = juce::jlimit (0.0, 1.0, p.presence + 0.08);
        p2.treble = juce::jlimit (0.0, 1.0, p.treble - 0.05);
        p2.mid = juce::jlimit (0.0, 1.0, p.mid + 0.06);
        p2.tight = juce::jlimit (0.0, 1.0, p.tight + 0.05);
    }
    if (bassMode) { bass[0].setParams (p); bass[1].setParams (p2); }
    else          { guitar[0].setParams (p); guitar[1].setParams (p2); }
    const int delaySamples = (wide && ! bassMode) ? (int) std::lround (0.011 * getSampleRate()) : 0;

    // Gate: aus bei 0, sonst Schwelle zwischen -75 dBFS (sanft) und -35 dBFS (streng)
    for (auto& gt : gates)
    {
        gt.setEnabled (p.gate > 0.01);
        gt.setThreshold (-75.0 + 40.0 * p.gate);
    }

    auto amp = [&] (int ch, float x) { return bassMode ? bass[(size_t) ch].process (x) : guitar[(size_t) ch].process (x); };
    float peak = 0.0f, gSum = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        // Automatischer Eingangspegel: lernt, wie laut gespielt wird, und bringt es auf den Pegel,
        // fuer den die Presets abgestimmt sind.
        const double a = std::max (std::abs ((double) L[i]), std::abs ((double) (R ? R[i] : L[i])));
        if (a > held) held = a;
        else if (a > held * 0.5) held *= heldDecay;
        const double tg = targetGain();
        inGain += (tg < inGain ? levelAttack : levelRelease) * (tg - inGain);
        const float l = (float) (L[i] * inGain), r = R ? (float) (R[i] * inGain) : l;

        double ol, orr, g0, g1;
        if (! monoInput)
        {
            g0 = gates[0].process (std::abs (l));
            g1 = gates[1].process (std::abs (r));
            ol = amp (0, l) * g0;
            orr = amp (1, r) * g1;
        }
        else
        {
            const float m = 0.5f * (l + r);
            g0 = gates[0].process (std::abs (m));
            ol = amp (0, m) * g0;
            if (wide)
            {
                delayLine[(size_t) delayPos] = m;
                int rp = delayPos - delaySamples; if (rp < 0) rp += (int) delayLine.size();
                const float md = delayLine[(size_t) rp];
                delayPos = (delayPos + 1) % (int) delayLine.size();
                g1 = gates[1].process (std::abs (md));
                orr = amp (1, md) * g1;
            }
            else { g1 = g0; orr = ol; }
        }
        gSum += (float) std::max (g0, g1);
        L[i] = (float) ol;
        if (R) R[i] = (float) orr;
        peak = std::max (peak, std::max (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f));
    }
    if (! bassMode && p.cab && ! forceNoCab)
    {
        juce::dsp::AudioBlock<float> blockAll (buffer);
        auto block = blockAll.getSubsetChannelBlock (0, (size_t) numCh);
        cabConv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    // Stereo-Doppeln: Tiefen in der Mitte halten (sonst wird es in Mono duenn)
    if (wide && R != nullptr)
    {
        sideHp.c = ironcurve::makeCoeffs (ironcurve::FilterType::HighPass, 140.0, 0.707, 0.0, getSampleRate());
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float d = sideHp.process (0.5f * (L[i] - R[i]));
            L[i] = (m + d) * 1.19f; R[i] = (m - d) * 1.19f; // gleich laut wie Mono
        }
    }
    // Sicherheit: weiche Begrenzung ab -1 dBFS, damit nichts uebersteuert
    peak = 0.0f;
    for (int c = 0; c < numCh; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i], ax = std::abs (x), knee = 0.89f;
            if (ax > knee) d[i] = std::copysign (knee + (1.0f - knee) * std::tanh ((ax - knee) / (1.0f - knee)), x) * 0.995f;
            peak = std::max (peak, std::abs (d[i]));
        }
    }
    gateMeter.store (n > 0 ? gSum / (float) n : 0.0f);
    if (peak > outMeter.load()) outMeter.store (peak);
}

juce::AudioBuffer<float> AmpProcessor::makeCabIR (double fs)
{
    // Minimalphasiger Lautsprecher-Impuls aus der Klangkurve (Cepstrum-Verfahren)
    const int order = fs > 60000.0 ? 13 : 12; // 4096 bzw. 8192 Punkte
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = juce::jmax (1.0, k * fs / N);
        const double pos = 12.0 * std::log2 (f / 20.0);
        double db;
        if (pos <= 0.0) db = kCabCurveDb[0] - 12.0 * (-pos / 12.0); // unter 20 Hz weiter abfallen
        else if (pos >= kCabCurvePoints - 1) db = kCabCurveDb[kCabCurvePoints - 1] - 12.0 * (pos - (kCabCurvePoints - 1)) / 12.0;
        else
        {
            const int i = (int) pos; const double t = pos - i;
            db = kCabCurveDb[i] * (1.0 - t) + kCabCurveDb[i + 1] * t;
        }
        const float lnMag = (float) (db / 20.0 * std::log (10.0));
        a[(size_t) k] = { lnMag, 0.0f };
        if (k > 0 && k < N / 2) a[(size_t) (N - k)] = { lnMag, 0.0f };
    }
    fft.perform (a.data(), b.data(), true); // reelles Cepstrum
    for (int n = 0; n < N; ++n)
    {
        const float w = (n == 0 || n == N / 2) ? 1.0f : (n < N / 2 ? 2.0f : 0.0f);
        a[(size_t) n] = b[(size_t) n] * w;
    }
    fft.perform (a.data(), b.data(), false);
    for (auto& c : b) c = std::exp (c);
    fft.perform (b.data(), a.data(), true);

    const int len = juce::jmin (N, (int) std::lround (0.045 * fs)); // 45 ms reichen fuer eine Box
    juce::AudioBuffer<float> ir (1, len);
    const int fade = len / 4;
    for (int n = 0; n < len; ++n)
    {
        float w = 1.0f;
        if (n >= len - fade) w = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) (n - (len - fade)) / (float) fade));
        ir.setSample (0, n, a[(size_t) n].real() * w);
    }
    return ir;
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
