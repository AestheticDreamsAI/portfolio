// Synthetische DI-Signale (Karplus-Strong-Saitenmodell) fuer Tests und Hoerproben
#pragma once

#include <cmath>
#include <random>
#include <vector>

namespace amptest
{
    constexpr double kPi = 3.14159265358979323846;

    // Eine gezupfte Saite: Rauschimpuls in Verzoegerungsleitung, Mittelwertfilter mit Daempfung
    struct KsString
    {
        std::vector<double> buf;
        size_t pos = 0;
        double damp = 0.996, last = 0.0;

        void pluck (double freq, double fs, double amp, double damping, std::mt19937& rng, double brightness = 0.7)
        {
            const size_t n = std::max<size_t> (2, (size_t) std::round (fs / freq));
            buf.assign (n, 0.0);
            std::uniform_real_distribution<double> u (-1.0, 1.0);
            double lp = 0.0;
            for (auto& v : buf) { lp = brightness * u (rng) + (1.0 - brightness) * lp; v = amp * lp; }
            pos = 0; damp = damping; last = 0.0;
        }
        double next()
        {
            if (buf.empty()) return 0.0;
            const size_t nx = (pos + 1) % buf.size();
            const double y = buf[pos];
            buf[pos] = damp * 0.5 * (buf[pos] + buf[nx]);
            pos = nx;
            return y;
        }
    };

    // Gitarren-Riff in Drop C: 16tel-Chugs (gedaempft) im Wechsel mit klingenden Powerchords, 160 BPM
    inline std::vector<float> guitarRiff (double fs, double seconds, unsigned seed = 1, double noiseFloorDb = -72.0)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<double> nd (0.0, 1.0);
        const int total = (int) (fs * seconds);
        std::vector<float> out ((size_t) total, 0.0f);
        const double sixteenth = 60.0 / 160.0 / 4.0;
        const double C2 = 65.41;
        KsString s[3];
        const double noiseAmp = std::pow (10.0, noiseFloorDb / 20.0);
        int step = 0;
        double nextT = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double t = i / fs;
            if (t >= nextT)
            {
                const int bar = (step / 16) % 4, pos16 = step % 16;
                const bool chord = (pos16 == 0 || pos16 == 8) && bar % 2 == 1;   // klingender Akkord
                const bool chug = ! chord && (pos16 % 2 == 0 || pos16 >= 12) && pos16 != 6 && pos16 != 7; // Pausen fuer das Gate
                double root = C2 * (bar == 2 ? std::pow (2.0, 3.0 / 12.0) : 1.0);
                if (chord || chug)
                {
                    const double damping = chord ? 0.9993 : 0.985;
                    const double amp = chord ? 0.30 : 0.24;
                    s[0].pluck (root, fs, amp, damping, rng, 0.75);
                    s[1].pluck (root * 1.4983, fs, amp * 0.8, damping, rng, 0.75); // Quinte
                    s[2].pluck (root * 2.0, fs, amp * 0.6, damping, rng, 0.75);    // Oktave
                }
                else if (pos16 == 6)
                {
                    for (auto& k : s) k.damp = 0.3; // Saiten abstoppen -> echte Pause
                }
                ++step;
                nextT += sixteenth;
            }
            double v = 0.0;
            for (auto& k : s) v += k.next();
            v += noiseAmp * nd (rng) + noiseAmp * 0.5 * std::sin (2.0 * kPi * 50.0 * t); // Rauschen + Brumm
            out[(size_t) i] = (float) v;
        }
        return out;
    }

    // Bass-Riff in Drop C (C1 = 32,7 Hz): Achtel, gezupft
    inline std::vector<float> bassRiff (double fs, double seconds, unsigned seed = 2)
    {
        std::mt19937 rng (seed);
        const int total = (int) (fs * seconds);
        std::vector<float> out ((size_t) total, 0.0f);
        const double eighth = 60.0 / 160.0 / 2.0;
        const double C1 = 32.70;
        const double pattern[8] = { 1, 1, 1.189, 1, 1, 1.335, 1.189, 1 };
        KsString s;
        int step = 0;
        double nextT = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double t = i / fs;
            if (t >= nextT)
            {
                s.pluck (C1 * pattern[step % 8] * 2.0, fs, 0.45, 0.9975, rng, 0.5);
                ++step;
                nextT += eighth;
            }
            out[(size_t) i] = (float) s.next();
        }
        return out;
    }
}
