// End-to-End-Test: synthetisches Gitarrensignal -> Lernen -> Parameter pruefen -> Screenshot
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <random>

using namespace ironcurve;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    const int block = 1600; // 1/30 s

    IroncurveProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.prepareToPlay (fs, block);

    // "Matschige" High-Gain-Gitarre: Rauschen, geformt mit Mulm-Buckel,
    // schmaler 3,2-kHz-Resonanz, Fizz und Speaker-Rolloff
    std::vector<Biquad> shaper (6);
    shaper[0].c = makeCoeffs (FilterType::HighPass, 80, 0.7, 0, fs);
    shaper[1].c = makeCoeffs (FilterType::Peak, 400, 1.0, 6.0, fs);
    shaper[2].c = makeCoeffs (FilterType::Peak, 3200, 9.0, 10.0, fs);
    shaper[3].c = makeCoeffs (FilterType::Peak, 7000, 1.2, 5.0, fs);
    shaper[4].c = makeCoeffs (FilterType::LowPass, 9000, 0.7, 0, fs);
    shaper[5].c = makeCoeffs (FilterType::LowShelf, 250, 0.7, 4.0, fs);

    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.08f);
    float pinkState[3] = {};

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;

    auto feed = [&]
    {
        for (int i = 0; i < block; ++i)
        {
            // grob rosa: Rauschen + einfache Tiefpass-Stufen
            const float w = noise (rng);
            pinkState[0] = 0.997f * pinkState[0] + 0.029591f * w;
            pinkState[1] = 0.985f * pinkState[1] + 0.032534f * w;
            pinkState[2] = 0.950f * pinkState[2] + 0.048056f * w;
            float x = pinkState[0] + pinkState[1] + pinkState[2] + 0.05f * w;
            for (auto& f : shaper)
                x = f.process (x);
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
    };

    proc.apvts.getParameter ("mode")->setValueNotifyingHost (0.0f); // Rhythmus-Gitarre
    proc.startLearning();

    int iterations = 0;
    while (proc.getAnalyzer().isLearning() && iterations < 900)
    {
        feed();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
        ++iterations;
    }

    std::printf ("Lernen beendet nach %d Bloecken, Status: %s\n", iterations, proc.getStatusText().toRawUTF8());

    const auto eq = proc.getCurrentEqSettings();
    const char* names[] = { "LowCut", "LowShelf", "Bell1", "Bell2", "Bell3", "Bell4", "Res1", "Res2", "HighShelf", "HighCut" };
    for (int b = 0; b < kNumBands; ++b)
        if (eq.bands[(size_t) b].enabled)
            std::printf ("  %-9s %8.1f Hz  %+5.1f dB  Q %.2f\n", names[b], eq.bands[(size_t) b].freq, eq.bands[(size_t) b].gainDb, eq.bands[(size_t) b].q);
    std::printf ("  Ausgang %+.1f dB\n", proc.apvts.getRawParameterValue ("output")->load());

    int fails = 0;
    bool notch = false;
    for (int b : { kRes1, kRes2 })
        notch |= eq.bands[(size_t) b].enabled && std::abs (eq.bands[(size_t) b].freq / 3200.0 - 1.0) < 0.06 && eq.bands[(size_t) b].gainDb < -3.0;
    if (! notch) { std::printf ("FAIL: keine Notch bei 3,2 kHz\n"); ++fails; }
    if (eqResponseDb (eq, 1.0, 400, fs) > -2.0) { std::printf ("FAIL: 400 Hz nicht abgesenkt\n"); ++fails; }
    if (eqResponseDb (eq, 1.0, 7000, fs) > -1.5) { std::printf ("FAIL: Fizz nicht abgesenkt\n"); ++fails; }

    // Noch ein paar Bloecke, damit die Live-Anzeige gefuellt ist, dann Screenshot
    for (int i = 0; i < 40; ++i)
    {
        feed();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
    }

    // Ausgang muss endlich und nicht stumm sein
    float peak = buf.getMagnitude (0, block);
    if (! std::isfinite (peak) || peak < 1.0e-4f) { std::printf ("FAIL: Ausgang ungueltig\n"); ++fails; }

    if (argc > 1)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File out { juce::String (argv[1]) };
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("Screenshot: %s\n", argv[1]);
    }

    std::printf (fails == 0 ? "End-to-End: OK\n" : "End-to-End: %d Fehler\n", fails);
    proc.releaseResources();
    return fails == 0 ? 0 : 1;
}
