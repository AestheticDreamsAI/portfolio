// Ironcurve Master - automatischer Mix-Bus / Master
//
// Kette: Auto-EQ -> Schaerfe-Schutz (dyn. EQ) -> Mono-Bass/Breite -> Glue-Kompressor -> Drive -> Limiter
// Lernen in vier Phasen (ca. 17 s Musik):
//   1. Spektrum      -> EQ (Mix-Bus-Zielkurve) + Frequenz des Schaerfe-Schutzes
//   2. Dynamik       -> Kompressor-Schwelle + Schaerfe-Schwelle
//   3. Lautheit      -> Drive, damit die Ziel-Lautheit erreicht wird
//   4. Kontrolle     -> Messung nach dem Limiter, Drive feinjustieren
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>

#include "../DSP/AutoEqFitter.h"
#include "../DSP/Dynamics.h"
#include "../Shared/EqChain.h"
#include "../Shared/EqDisplay.h"
#include "../Shared/EqParams.h"
#include "../SpectrumAnalyzer.h"

class MasterProcessor : public juce::AudioProcessor, public EqViewSource, private juce::Timer
{
public:
    MasterProcessor();
    ~MasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ironcurve Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Lernen (Message-Thread) ----
    enum class Phase { Idle, Spectrum, Dynamics, Loudness, Verify };
    void startLearning();
    void cancelLearning();
    void resetAll();
    bool isLearning() const { return phase != Phase::Idle; }
    float getLearnProgress() const;
    juce::String getPhaseName() const;
    juce::String getStatusText() const { return statusText; }

    static const std::array<float, 4> targetLufs;
    float getTargetLufs() const;

    // ---- Meter (Message-Thread) ----
    struct Meters { float shortTermLufs = -100.0f, deharshGr = 0, compGr = 0, limitGr = 0; };
    Meters getMeters() const { return meters; }

    // ---- EqViewSource ----
    juce::AudioProcessorValueTreeState& getApvts() override { return apvts; }
    SpectrumAnalyzer& getAnalyzer() override { return analyzer; }
    ironcurve::EqSettings getCurrentEqSettings() const override { return eqChain.getSettings(); }
    float getAmount() const override { return eqAmount->load(); }
    double getAnalysisRate() const override { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }
    bool hasLearnedSpectrum() const override;
    std::vector<double> getLearnedSpectrum() const override;
    std::vector<double> getAlignedTarget() const override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void onSpectrumLearned (std::vector<double> db);
    void enterPhase (Phase p);
    void setParam (const juce::String& id, float v) { eqparams::setParam (apvts, id, v); }

    SpectrumAnalyzer analyzer;
    EqChain eqChain;

    ironcurve::DeHarsher deharsher;
    ironcurve::StereoShaper shaper;
    ironcurve::Compressor comp;
    ironcurve::Limiter limiter;
    ironcurve::KWeighting kw;
    ironcurve::KWeightedChannel kPre[2], kPost[2];

    std::atomic<float> *eqAmount, *target, *deharshAmt, *dhFreq, *dhThr, *monoOn, *monoFreq, *width,
                       *glue, *compThr, *drive, *ceiling;
    float lastDhFreq = -1.0f, lastMonoFreq = -1.0f, lastMonoOn = -1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmooth;

    // 100-ms-Messframes vom Audio-Thread
    struct Frame { double msPre = 0, msPost = 0, compLevelDb = 0, harshDb = 0; };
    juce::AbstractFifo frameFifo { 512 };
    std::array<Frame, 512> frameBuf;
    Frame acc;
    int accCount = 0, frameLen = 4800;

    std::atomic<float> grDeharsh { 0 }, grComp { 0 }, grLimit { 0 };

    // Message-Thread-Zustand
    Phase phase = Phase::Idle;
    int skipFrames = 0, phaseFrames = 0, verifyRound = 0;
    std::vector<Frame> phaseData;
    std::deque<double> shortTerm;
    double limitGrSum = 0.0;
    Meters meters;
    juce::String statusText;

    mutable juce::CriticalSection learnedLock;
    std::vector<double> learnedDb, alignedTargetDb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterProcessor)
};
