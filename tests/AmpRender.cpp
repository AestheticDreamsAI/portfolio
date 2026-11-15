// Rendert synthetische Hoerproben durch Ironcurve Amp
#include "../Source/Amp/AmpProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "AmpTestSignals.h"
using namespace amptest;
static void render (int preset, const std::vector<float>& L, const std::vector<float>& R, const juce::String& path)
{
    const double fs = 48000; const int n = (int) L.size(), blk = 512;
    AmpProcessor p; p.setPlayConfigDetails (2, 2, fs, blk);
    p.apvts.getParameter ("preset")->setValueNotifyingHost (p.apvts.getParameter ("preset")->convertTo0to1 ((float) preset));
    p.prepareToPlay (fs, blk);
    juce::AudioBuffer<float> out (2, n); juce::MidiBuffer m;
    for (int s = 0; s < n; s += blk)
    {
        const int len = juce::jmin (blk, n - s);
        juce::AudioBuffer<float> b (2, len);
        b.copyFrom (0, 0, L.data() + s, len); b.copyFrom (1, 0, R.data() + s, len);
        p.processBlock (b, m);
        out.copyFrom (0, s, b, 0, 0, len); out.copyFrom (1, s, b, 1, 0, len);
    }
    juce::File f (path); f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), fs, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, n);
}
int main (int, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String dir (argv[1]);
    auto gL = guitarRiff (48000, 12, 1), gR = guitarRiff (48000, 12, 3);
    auto b = bassRiff (48000, 12, 2);
    for (int i = 0; i < ironcurve::kNumAmpPresets; ++i)
    {
        const auto& pr = ironcurve::ampPresets()[(size_t) i];
        const auto name = juce::String (i + 1).paddedLeft ('0', 2) + "_" + juce::String::fromUTF8 (pr.name).replace (" & ", "_").replace (" ", "_");
        if (pr.bass) render (i, b, b, dir + "/" + name + ".wav");
        else         render (i, gL, gR, dir + "/" + name + ".wav");
    }
    return 0;
}
