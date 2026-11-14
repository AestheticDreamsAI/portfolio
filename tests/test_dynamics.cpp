// Tests fuer die Dynamik-Bausteine von Ironcurve Master
#include "../Source/DSP/Dynamics.h"

#include <cstdio>
#include <random>

using namespace ironcurve;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else { std::printf ("  ok:   %s\n", msg); } } while (0)

int main()
{
    const double fs = 48000.0;

    // --- K-Weighting: 1-kHz-Sinus, Amplitude 1, ein Kanal -> ca. -3,01 LUFS ----
    {
        std::printf ("K-Weighting / LUFS\n");
        KWeighting k; k.prepare (fs);
        KWeightedChannel ch; ch.setup (k);
        std::vector<double> frames;
        const int frameLen = (int) (0.1 * fs);
        double acc = 0.0; int n = 0;
        for (int i = 0; i < (int) (5 * fs); ++i)
        {
            const float x = (float) std::sin (2.0 * kPi * 1000.0 * i / fs);
            const float y = ch.process (x);
            acc += (double) y * y;
            if (++n == frameLen) { frames.push_back (acc / n); acc = 0; n = 0; }
        }
        frames.erase (frames.begin()); // Einschwingen
        const double lufs = gatedLoudness (frames);
        std::printf ("    1 kHz Sinus 0 dBFS: %.2f LUFS\n", lufs);
        CHECK (std::abs (lufs - (-3.01)) < 0.1, "LUFS-Messung stimmt mit BS.1770 ueberein");
    }

    // --- Limiter: keine Overs, korrekte Latenz ---------------------------------
    {
        std::printf ("Limiter\n");
        Limiter lim; lim.prepare (fs); lim.setCeilingDb (-1.0);
        const double ceil = dbToGain (-1.0);
        std::mt19937 rng (3);
        std::normal_distribution<float> nd (0.0f, 1.0f);
        double maxOut = 0.0;
        for (int i = 0; i < (int) (10 * fs); ++i)
        {
            // Lautes Rauschen mit zufaelligen Spitzen, +12 dB Drive
            float l = nd (rng) * 0.6f * 4.0f, r = nd (rng) * 0.6f * 4.0f;
            if (i % 9973 == 0) { l = 8.0f; r = -8.0f; }
            lim.process (l, r);
            maxOut = std::max (maxOut, (double) std::max (std::abs (l), std::abs (r)));
        }
        std::printf ("    max. Ausgang %.4f (Ceiling %.4f)\n", maxOut, ceil);
        CHECK (maxOut <= ceil + 1e-6, "Kein Sample ueber dem Ceiling");

        // Latenz: Impuls unter dem Ceiling muss genau getLatencySamples() spaeter kommen
        Limiter lim2; lim2.prepare (fs); lim2.setCeilingDb (-1.0);
        int found = -1;
        for (int i = 0; i < 2000; ++i)
        {
            float l = (i == 100) ? 0.5f : 0.0f, r = l;
            lim2.process (l, r);
            if (l > 0.25f && found < 0) found = i - 100;
        }
        std::printf ("    Latenz %d Samples (gemeldet %d)\n", found, lim2.getLatencySamples());
        CHECK (found == lim2.getLatencySamples(), "Gemeldete Latenz stimmt (wichtig fuer FLs Delay-Kompensation)");

        // Leiser Sinus wird nicht veraendert
        Limiter lim3; lim3.prepare (fs); lim3.setCeilingDb (-1.0);
        double maxDiff = 0.0;
        std::vector<float> hist;
        for (int i = 0; i < 20000; ++i)
        {
            const float x = 0.3f * (float) std::sin (2.0 * kPi * 440.0 * i / fs);
            hist.push_back (x);
            float l = x, r = x;
            lim3.process (l, r);
            const int j = i - lim3.getLatencySamples();
            if (j >= 0) maxDiff = std::max (maxDiff, (double) std::abs (l - hist[(size_t) j]));
        }
        CHECK (maxDiff < 1e-6, "Signal unter dem Ceiling bleibt unveraendert");

        // True Peak: Sinus knapp unter fs/4 mit 45 Grad Phase hat Spitzen zwischen den Samples
        Limiter lim4; lim4.prepare (fs); lim4.setCeilingDb (-1.0);
        std::vector<float> outSig;
        for (int i = 0; i < (int) fs; ++i)
        {
            const float x = 3.0f * (float) std::sin (2.0 * kPi * 11025.0 * i / fs + 0.785) * (float) (0.5 + 0.5 * std::sin (2.0 * kPi * 3.0 * i / fs));
            float l = x, r = x;
            lim4.process (l, r);
            outSig.push_back (l);
        }
        // Referenz-Interpolation mit langem Sinc (16x) als "echter" True Peak
        double tp = 0.0;
        for (size_t n = 64; n + 64 < outSig.size(); ++n)
            for (int ph = 0; ph < 16; ++ph)
            {
                double y = 0.0;
                for (int k = -48; k <= 48; ++k)
                {
                    const double t = k - ph / 16.0;
                    const double sc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                    y += sc * (0.42 + 0.5 * std::cos (kPi * t / 49.0) + 0.08 * std::cos (2 * kPi * t / 49.0)) * outSig[(size_t) ((long) n + k)];
                }
                tp = std::max (tp, std::abs (y));
            }
        std::printf ("    True Peak %.2f dBTP (Ceiling -1.00)\n", gainToDb (tp));
        CHECK (gainToDb (tp) <= -0.9, "True Peak bleibt am Ceiling (max. 0,1 dB Toleranz)");
    }

    // --- Kompressor: 2:1 oberhalb der Schwelle -----------------------------------
    {
        std::printf ("Kompressor\n");
        Compressor c; c.prepare (fs); c.setParams (-20.0, 2.0, 0.0);
        double g = 1.0;
        for (int i = 0; i < (int) (2 * fs); ++i)
        {
            const float x = 0.1f * (float) std::sin (2.0 * kPi * 200.0 * i / fs) * 1.41421356f; // RMS -20+?
            g = c.process (x * 3.1623f, x * 3.1623f); // RMS -10 dBFS
        }
        const double grDb = -gainToDb (g);
        std::printf ("    10 dB ueber Schwelle bei 2:1 -> %.2f dB Reduktion\n", grDb);
        CHECK (std::abs (grDb - 5.0) < 0.5, "Ratio wird eingehalten");
    }

    // --- De-Harsher: greift nur bei zu viel 4 kHz --------------------------------
    {
        std::printf ("De-Harsher\n");
        auto run = [&] (double harshGain)
        {
            DeHarsher d; d.prepare (fs); d.setFrequency (4000.0); d.setParams (-12.0, 6.0);
            double maxGr = 0.0;
            for (int i = 0; i < (int) (2 * fs); ++i)
            {
                float x = 0.3f * (float) std::sin (2.0 * kPi * 150.0 * i / fs)
                        + (float) harshGain * (float) std::sin (2.0 * kPi * 4000.0 * i / fs);
                float l = x, r = x;
                d.process (l, r);
                maxGr = std::max (maxGr, d.currentGainReductionDb());
            }
            return maxGr;
        };
        const double calm = run (0.02), harsh = run (0.3);
        std::printf ("    ruhig: %.2f dB, scharf: %.2f dB\n", calm, harsh);
        CHECK (calm < 0.5, "Ruhiges Signal bleibt unberuehrt");
        CHECK (harsh > 3.0 && harsh <= 6.0 + 1e-9, "Scharfes Signal wird begrenzt abgesenkt");
    }

    // --- Mono-Bass ---------------------------------------------------------------
    {
        std::printf ("Stereo-Shaper\n");
        StereoShaper s; s.prepare (fs); s.setMonoBelow (120.0, true); s.setWidth (1.0);
        double sideLow = 0.0, sideHigh = 0.0;
        for (int i = 0; i < (int) fs; ++i)
        {
            // Reines Seitensignal: 50 Hz und 2 kHz
            float sl = (float) std::sin (2.0 * kPi * 50.0 * i / fs), sh = (float) std::sin (2.0 * kPi * 2000.0 * i / fs);
            float l1 = sl, r1 = -sl; s.process (l1, r1);
            if (i > fs / 2) sideLow = std::max (sideLow, (double) std::abs (l1 - r1) * 0.5);
            (void) sh;
        }
        StereoShaper s2; s2.prepare (fs); s2.setMonoBelow (120.0, true); s2.setWidth (1.0);
        for (int i = 0; i < (int) fs; ++i)
        {
            float sh = (float) std::sin (2.0 * kPi * 2000.0 * i / fs);
            float l1 = sh, r1 = -sh; s2.process (l1, r1);
            if (i > fs / 2) sideHigh = std::max (sideHigh, (double) std::abs (l1 - r1) * 0.5);
        }
        std::printf ("    Seite 50 Hz: %.1f dB, 2 kHz: %.1f dB\n", gainToDb (sideLow), gainToDb (sideHigh));
        CHECK (gainToDb (sideLow) < -12.0, "Bass unter 120 Hz wird mono");
        CHECK (std::abs (gainToDb (sideHigh)) < 0.5, "Hoehen behalten ihre Breite");
    }

    std::printf (failures == 0 ? "\nAlle Dynamik-Tests bestanden.\n" : "\n%d Test(s) fehlgeschlagen.\n", failures);
    return failures == 0 ? 0 : 1;
}
