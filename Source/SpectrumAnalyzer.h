// Ironcurve Auto EQ - Spektrumanalyse (Anzeige + Lernphase)
//
// Der Audio-Thread schreibt nur lock-free in einen FIFO. Die FFT laeuft
// in einem Timer auf dem Message-Thread, damit der Audio-Thread nie blockiert.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <vector>

class SpectrumAnalyzer : private juce::Timer
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize  = 1 << fftOrder; // 8192
    static constexpr int hopSize  = fftSize / 4;   // 75 % Overlap

    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;

    void prepare (double sampleRate);

    // Audio-Thread
    void pushAudio (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept;

    // Message-Thread
    void startLearning (double secondsOfSignal);
    void cancelLearning();
    bool isLearning() const noexcept   { return learning; }
    float getLearnProgress() const noexcept;
    bool hasRecentSignal() const noexcept { return framesSinceSignal < 30; }

    // Leistung pro 1/24-Okt.-Band in dB (pink-flach), fuer die Live-Anzeige
    const std::vector<float>& getDisplayDb() const noexcept { return displayDb; }

    std::function<void (std::vector<double>)> onLearnFinished;

    // Analysiert eine komplette Audiodatei mit derselben Methode wie das Live-Lernen
    // (Leistungsmittel aller Frames ueber -50 dBFS). Leer, wenn nichts Hoerbares drin ist.
    static std::vector<double> analyzeFile (juce::AudioFormatReader& reader);

private:
    void timerCallback() override;
    void processFrame();
    void rebuildBandMap (double sampleRate);

    juce::AbstractFifo fifo { 1 << 17 };
    std::vector<float> fifoBuffer;

    std::vector<float> hopBuffer;
    int hopFill = 0;
    std::vector<float> history;
    std::vector<float> fftData;
    std::vector<float> window;
    std::vector<double> binPower;
    std::vector<double> bandPower;
    juce::dsp::FFT fft { fftOrder };
    double windowNorm = 1.0;

    std::atomic<double> currentRate { 48000.0 };
    double mappedRate = 0.0;
    struct BandMap { int binLo, binHi; double centreBin, widthBins; bool valid; };
    std::vector<BandMap> bandMap;

    std::vector<float> displayDb;
    int framesSinceSignal = 1000;

    bool learning = false;
    double learnSeconds = 8.0;
    int learnFrames = 0;
    std::vector<double> learnAccum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
