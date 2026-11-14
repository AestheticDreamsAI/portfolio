// Stresstest: Audio-Thread verarbeitet ohne Pause (schneller als Echtzeit, wie beim Export),
// waehrend der Message-Thread den Editor oeffnet. Der Message-Thread muss reaktionsfaehig bleiben.
#include "../Source/PluginProcessor.h"
#include "../Source/Master/MasterProcessor.h"

#include <thread>

template <typename Proc>
static bool stress (const char* name)
{
    Proc proc;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, 48000.0, block);
    proc.prepareToPlay (48000.0, block);

    std::atomic<bool> run { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (1);
        while (run.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, rng.nextFloat() * 0.5f - 0.25f);
            proc.processBlock (buf, midi);
        }
    });

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);

    // 3 s lang messen, wie schnell asynchrone Nachrichten auf dem Message-Thread ankommen
    double worstMs = 0.0;
    const auto end = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
    {
        std::atomic<bool> done { false };
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        juce::MessageManager::callAsync ([&] { done = true; });
        while (! done.load() && juce::Time::getMillisecondCounterHiRes() - t0 < 5000.0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        worstMs = std::max (worstMs, juce::Time::getMillisecondCounterHiRes() - t0);
        if (! done.load())
            break;
    }

    run = false;
    audio.join();
    ed.reset();
    std::printf ("%-20s schlechteste Reaktionszeit des Message-Threads: %.0f ms\n", name, worstMs);
    return worstMs < 1000.0;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool a = stress<IroncurveProcessor> ("Ironcurve Auto EQ");
    const bool b = stress<MasterProcessor> ("Ironcurve Master");
    std::printf (a && b ? "Stresstest: OK\n" : "Stresstest: FEHLER (Oberflaeche friert ein)\n");
    return a && b ? 0 : 1;
}
