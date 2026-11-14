// Rendert synthetische Hoerproben durch Ironcurve Amp
#include "../Source/Amp/AmpProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "AmpTestSignals.h"
using namespace amptest;
static void render (int mode, const std::vector<float>& L, const std::vector<float>& R, const juce::String& path)
{
    const double fs = 48000; const int n = (int) L.size(), blk = 512;
    AmpProcessor p; p.setPlayConfigDetails (2, 2, fs, blk);
    p.apvts.getParameter ("mode")->setValueNotifyingHost ((float) mode);
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
    render (0, gL, gR, dir + "/Ironcurve_Amp_Gitarre_Probe.wav");
    auto b = bassRiff (48000, 12, 2);
    render (1, b, b, dir + "/Ironcurve_Amp_Bass_Probe.wav");
    return 0;
}
