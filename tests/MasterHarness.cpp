// Ironcurve Master - End-to-End mit einer echten WAV-Datei
//   MasterHarness <in.wav> <out.wav> <zielIndex 0..3> [screenshot.png]
// Lernt auf einer Schleife des lautesten Abschnitts (Echtzeit-Takt, damit die
// Lernphasen wie im DAW ablaufen) und rendert danach den kompletten Track.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Master/MasterProcessor.h"
#include "../Source/Master/MasterEditor.h"

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf ("usage: MasterHarness in.wav out.wav targetIndex [shot.png] [reference.wav]\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (juce::String (argv[1]))));
    if (reader == nullptr) { std::printf ("Kann Datei nicht lesen\n"); return 2; }

    const double fs = reader->sampleRate;
    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> src (2, len);
    reader->read (&src, 0, len, 0, true, true);

    const int block = (int) (fs / 30.0);
    MasterProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.prepareToPlay (fs, block);
    proc.apvts.getParameter ("target")->setValueNotifyingHost (proc.apvts.getParameter ("target")->convertTo0to1 ((float) std::atoi (argv[3])));

    // Lautesten 20-s-Abschnitt finden (RMS)
    int bestStart = 0;
    {
        double best = -1.0;
        const int win = (int) (20 * fs), hop = (int) fs;
        for (int s = 0; s + win <= len || s == 0; s += hop)
        {
            const int n = std::min (win, len - s);
            double e = 0.0;
            for (int ch = 0; ch < 2; ++ch) e += src.getRMSLevel (ch, s, n);
            if (e > best) { best = e; bestStart = s; }
            if (s + win > len) break;
        }
    }
    const int loopLen = std::min ((int) (20 * fs), len - bestStart);
    std::printf ("Lerne auf %.1f s – %.1f s\n", bestStart / fs, (bestStart + loopLen) / fs);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    int pos = 0;
    if (argc > 5)
    {
        const bool ok = proc.loadReference (juce::File (juce::String (argv[5])));
        std::printf ("Referenz: %s (%s)\n", ok ? "geladen" : "FEHLER", proc.getStatusText().toRawUTF8());
    }
    proc.startLearning();
    int iter = 0;
    while (proc.isLearning() && iter < 3000)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buf.setSample (ch, i, src.getSample (ch, bestStart + (pos + i) % loopLen));
        pos = (pos + block) % loopLen;
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
        ++iter;
    }
    std::printf ("Status: %s\n", proc.getStatusText().toRawUTF8());

    auto pv = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    std::printf ("Drive %+.1f dB | Komp-Schwelle %.1f dB | Schaerfe %.0f Hz, Schwelle %.1f dB\n",
                 pv ("drive"), pv ("comp_thr"), pv ("dh_freq"), pv ("dh_thr"));
    const auto eq = proc.getCurrentEqSettings();
    for (int b = 0; b < ironcurve::kNumBands; ++b)
        if (eq.bands[(size_t) b].enabled)
            std::printf ("  %-11s %8.1f Hz %+5.1f dB Q %.2f\n", eqparams::bandName (b).toRawUTF8(),
                         eq.bands[(size_t) b].freq, eq.bands[(size_t) b].gainDb, eq.bands[(size_t) b].q);

    // Kompletten Track rendern (frischer Zustand, gleiche Parameter)
    proc.prepareToPlay (fs, block);
    const int lat = proc.getLatencySamples();
    juce::AudioBuffer<float> out (2, len);
    juce::AudioBuffer<float> work (2, block);
    int written = -lat;
    for (int p = 0; p < len + lat; p += block)
    {
        const int n = block;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                work.setSample (ch, i, p + i < len ? src.getSample (ch, p + i) : 0.0f);
        proc.processBlock (work, midi);
        for (int i = 0; i < n; ++i, ++written)
            if (written >= 0 && written < len)
                for (int ch = 0; ch < 2; ++ch)
                    out.setSample (ch, written, work.getSample (ch, i));
    }

    juce::File outFile { juce::String (argv[2]) };
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (outFile), fs, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, len);
    w.reset();
    std::printf ("Geschrieben: %s (Latenz %d Samples kompensiert)\n", argv[2], lat);

    if (argc > 4)
    {
        // Ein paar Bloecke laufen lassen, damit Meter und Spektrum gefuellt sind
        for (int k = 0; k < 60; ++k)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, src.getSample (ch, bestStart + (pos + i) % loopLen));
            pos = (pos + block) % loopLen;
            proc.processBlock (buf, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        for (int k = 0; k < 10; ++k)
        {
            proc.processBlock (buf, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File shot { juce::String (argv[4]) };
        shot.deleteFile();
        juce::FileOutputStream os (shot);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    proc.releaseResources();
    return proc.isLearning() ? 1 : 0;
}
