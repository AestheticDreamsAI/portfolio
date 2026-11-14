#include "SpectrumAnalyzer.h"
#include "DSP/AutoEqFitter.h"

#include <cmath>

using ironcurve::AnalysisGrid;

SpectrumAnalyzer::SpectrumAnalyzer()
{
    fifoBuffer.resize ((size_t) fifo.getTotalSize(), 0.0f);
    hopBuffer.resize (hopSize, 0.0f);
    history.resize (fftSize, 0.0f);
    fftData.resize (fftSize * 2, 0.0f);
    binPower.resize (fftSize / 2 + 1, 0.0);
    bandPower.resize (AnalysisGrid::kNumBands, 0.0);
    displayDb.resize (AnalysisGrid::kNumBands, -150.0f);
    learnAccum.resize (AnalysisGrid::kNumBands, 0.0);

    window.resize (fftSize);
    double sumW = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        sumW += window[(size_t) i];
    }
    // Sinus mit Amplitude 1 -> 0 dB; Hann-ENBW (1,5 Bins) herausgerechnet
    windowNorm = 4.0 / (sumW * sumW) / 1.5;

    startTimerHz (30);
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stopTimer();
}

void SpectrumAnalyzer::prepare (double sampleRate)
{
    if (sampleRate > 0.0)
        currentRate.store (sampleRate);
}

void SpectrumAnalyzer::pushAudio (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    const float scale = 1.0f / (float) numChannels;
    auto writeMono = [&] (int dest, int srcOffset, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                s += buffer.getSample (ch, srcOffset + i);
            fifoBuffer[(size_t) (dest + i)] = s * scale;
        }
    };

    if (size1 > 0) writeMono (start1, 0, size1);
    if (size2 > 0) writeMono (start2, size1, size2);
    fifo.finishedWrite (size1 + size2); // bei vollem FIFO werden Samples verworfen
}

void SpectrumAnalyzer::rebuildBandMap (double sampleRate)
{
    mappedRate = sampleRate;
    const double binHz = sampleRate / fftSize;
    const int maxBin = fftSize / 2;

    bandMap.resize (AnalysisGrid::kNumBands);
    for (int b = 0; b < AnalysisGrid::kNumBands; ++b)
    {
        auto& m = bandMap[(size_t) b];
        const double lo = AnalysisGrid::lowerHz (b), hi = AnalysisGrid::upperHz (b);
        m.valid = hi < sampleRate * 0.5;
        m.binLo = (int) std::ceil (lo / binHz);
        m.binHi = std::min ((int) std::floor (hi / binHz), maxBin);
        m.centreBin = AnalysisGrid::centerHz (b) / binHz;
        m.widthBins = (hi - lo) / binHz;
    }
}

void SpectrumAnalyzer::timerCallback()
{
    const double rate = currentRate.load();
    if (rate != mappedRate)
        rebuildBandMap (rate);

    bool gotFrame = false;

    while (fifo.getNumReady() > 0)
    {
        const int wanted = hopSize - hopFill;
        int start1, size1, start2, size2;
        fifo.prepareToRead (wanted, start1, size1, start2, size2);

        if (size1 > 0) std::copy_n (fifoBuffer.begin() + start1, size1, hopBuffer.begin() + hopFill);
        if (size2 > 0) std::copy_n (fifoBuffer.begin() + start2, size2, hopBuffer.begin() + hopFill + size1);
        fifo.finishedRead (size1 + size2);
        hopFill += size1 + size2;

        if (hopFill == hopSize)
        {
            std::move (history.begin() + hopSize, history.end(), history.begin());
            std::copy (hopBuffer.begin(), hopBuffer.end(), history.end() - hopSize);
            hopFill = 0;
            processFrame();
            gotFrame = true;
        }
    }

    if (! gotFrame)
    {
        // Keine Daten (Transport gestoppt): Anzeige langsam ausblenden
        for (auto& d : displayDb)
            d = std::max (-150.0f, d - 1.5f);
        ++framesSinceSignal;
    }
}

void SpectrumAnalyzer::processFrame()
{
    // RMS des Fensters fuer das Gate
    double sumSq = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const float x = history[(size_t) i];
        sumSq += (double) x * x;
        fftData[(size_t) i] = x * window[(size_t) i];
    }
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    const double rmsDb = 10.0 * std::log10 (sumSq / fftSize + 1.0e-20);

    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    for (int k = 0; k <= fftSize / 2; ++k)
        binPower[(size_t) k] = (double) fftData[(size_t) k] * fftData[(size_t) k] * windowNorm;

    // Bins -> 1/24-Oktav-Baender (Leistungsdichte * Bandbreite = pink-flach)
    for (int b = 0; b < AnalysisGrid::kNumBands; ++b)
    {
        const auto& m = bandMap[(size_t) b];
        if (! m.valid)
        {
            bandPower[(size_t) b] = 1.0e-20;
            continue;
        }

        double density;
        if (m.binHi >= m.binLo)
        {
            double sum = 0.0;
            for (int k = m.binLo; k <= m.binHi; ++k)
                sum += binPower[(size_t) k];
            density = sum / (m.binHi - m.binLo + 1);
        }
        else
        {
            // Band schmaler als ein Bin: linear interpolieren
            const int k0 = juce::jlimit (0, fftSize / 2 - 1, (int) std::floor (m.centreBin));
            const double a = juce::jlimit (0.0, 1.0, m.centreBin - k0);
            density = binPower[(size_t) k0] * (1.0 - a) + binPower[(size_t) (k0 + 1)] * a;
        }
        bandPower[(size_t) b] = density * m.widthBins + 1.0e-20;
    }

    const bool active = rmsDb > -50.0;
    framesSinceSignal = active ? 0 : framesSinceSignal + 1;

    // Live-Anzeige: schneller Attack, langsamer Release
    for (int b = 0; b < AnalysisGrid::kNumBands; ++b)
    {
        const float db = (float) (10.0 * std::log10 (bandPower[(size_t) b]));
        float& d = displayDb[(size_t) b];
        d = db > d ? d + 0.6f * (db - d) : d + 0.15f * (db - d);
    }

    if (learning && active)
    {
        for (int b = 0; b < AnalysisGrid::kNumBands; ++b)
            learnAccum[(size_t) b] += bandPower[(size_t) b];
        ++learnFrames;

        if (getLearnProgress() >= 1.0f)
        {
            learning = false;
            std::vector<double> result ((size_t) AnalysisGrid::kNumBands);
            for (int b = 0; b < AnalysisGrid::kNumBands; ++b)
                result[(size_t) b] = 10.0 * std::log10 (learnAccum[(size_t) b] / learnFrames + 1.0e-20);

            if (onLearnFinished)
                onLearnFinished (std::move (result));
        }
    }
}

void SpectrumAnalyzer::startLearning (double secondsOfSignal)
{
    learnSeconds = juce::jmax (1.0, secondsOfSignal);
    std::fill (learnAccum.begin(), learnAccum.end(), 0.0);
    learnFrames = 0;
    learning = true;
}

void SpectrumAnalyzer::cancelLearning()
{
    learning = false;
}

float SpectrumAnalyzer::getLearnProgress() const noexcept
{
    const double secs = learnFrames * (double) hopSize / currentRate.load();
    return (float) juce::jlimit (0.0, 1.0, secs / learnSeconds);
}
