// Verarbeitet eine WAV-Datei mit Ironcurve Amp: AmpFile in.wav out.wav preset stereo
#include "../Source/Amp/AmpProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (argv[1])));
    const int n = (int) rd->lengthInSamples; const double fs = rd->sampleRate; const int blk = 512;
    juce::AudioBuffer<float> in (2, n); rd->read (&in, 0, n, 0, true, true);
    AmpProcessor p; p.setPlayConfigDetails (2, 2, fs, blk);
    auto* pp = p.apvts.getParameter ("preset"); pp->setValueNotifyingHost (pp->convertTo0to1 ((float) atoi (argv[3])));
    p.apvts.getParameter ("stereo")->setValueNotifyingHost (argc > 4 && atoi (argv[4]) ? 1.0f : 0.0f);
    p.forceNoCab = argc > 5 && atoi (argv[5]);
    p.prepareToPlay (fs, blk);
    juce::MidiBuffer m;
    for (int t = 0; t < 100 && ! p.isCabReady(); ++t)
    {
        juce::Thread::sleep (20);
        juce::AudioBuffer<float> z (2, blk); z.clear(); p.processBlock (z, m);
    }
    p.prepareToPlay (fs, blk); // Zustand sauber, Box geladen
    for (int t = 0; t < 100 && ! p.isCabReady(); ++t) { juce::Thread::sleep (20); juce::AudioBuffer<float> z (2, blk); z.clear(); p.processBlock (z, m); }
    for (int s = 0; s < n; s += blk)
    {
        const int len = juce::jmin (blk, n - s);
        juce::AudioBuffer<float> b (in.getArrayOfWritePointers(), 2, s, len);
        p.processBlock (b, m);
    }
    juce::File f (argv[2]); f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), fs, 2, 32, {}, 0));
    w->writeFromAudioSampleBuffer (in, 0, n);
    return 0;
}
