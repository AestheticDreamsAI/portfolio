// Tests fuer den Auto-EQ-Fitter mit synthetischen Spektren
#include "../Source/DSP/AutoEqFitter.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace ironcurve;

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else { std::printf ("  ok:   %s\n", msg); } } while (0)

static double gaussLog (double f, double centre, double sigmaOct, double gainDb)
{
    const double d = std::log2 (f / centre) / sigmaOct;
    return gainDb * std::exp (-0.5 * d * d);
}

static double focusError (const ModeProfile& p, const std::vector<double>& measured, const FitResult& r,
                          double fs, bool withEq)
{
    const int n = AnalysisGrid::kNumBands;
    std::vector<double> after ((size_t) n);
    for (int i = 0; i < n; ++i)
        after[(size_t) i] = measured[(size_t) i] + (withEq ? eqResponseDb (r.eq, 1.0, AnalysisGrid::centerHz (i), fs) : 0.0);

    const auto s = smoothDb (after, 4);
    double lo = std::max (p.focusLo, p.hpfFreq * 1.25), hi = std::min (p.focusHi, fs * 0.45);
    if (p.lpfFreq > 0) hi = std::min (hi, p.lpfFreq / 1.25);

    // Offset-unabhaengiger Fehler (nur die Form zaehlt)
    double mean = 0.0; int c = 0;
    for (int i = 0; i < n; ++i)
    {
        const double f = AnalysisGrid::centerHz (i);
        if (f >= lo && f <= hi) { mean += s[(size_t) i] - r.alignedTargetDb[(size_t) i]; ++c; }
    }
    mean /= c;

    double err = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double f = AnalysisGrid::centerHz (i);
        if (f >= lo && f <= hi)
        {
            const double d = s[(size_t) i] - r.alignedTargetDb[(size_t) i] - mean;
            err += d * d;
        }
    }
    return std::sqrt (err / c);
}

static void printEq (const FitResult& r)
{
    const char* names[] = { "HPF", "LowShelf", "Bell1", "Bell2", "Bell3", "Bell4", "Res1", "Res2", "HighShelf", "LPF" };
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = r.eq.bands[(size_t) i];
        if (b.enabled)
            std::printf ("    %-9s %8.1f Hz  %+5.1f dB  Q %.2f\n", names[i], b.freq, b.gainDb, b.q);
    }
    std::printf ("    Trim %+5.1f dB\n", r.outputTrimDb);
}

int main()
{
    const double fs = 48000.0;
    const int n = AnalysisGrid::kNumBands;
    const auto& profiles = modeProfiles();
    std::mt19937 rng (42);
    std::normal_distribution<double> noise (0.0, 0.4);

    // --- Test 1: matschige Rhythmus-Gitarre mit Fizz-Resonanz ---------------
    {
        std::printf ("Test 1: Rhythmus-Gitarre mit 400-Hz-Mulm und 3,2-kHz-Resonanz\n");
        const auto& p = profiles[0];
        std::vector<double> m ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double f = AnalysisGrid::centerHz (i);
            m[(size_t) i] = targetDbAt (p, f) - 30.0
                          + gaussLog (f, 400.0, 0.45, 5.0)       // Mulm
                          + gaussLog (f, 3200.0, 0.035, 9.0)     // schmale Resonanz
                          + gaussLog (f, 7000.0, 0.6, 4.0)       // Fizz
                          + noise (rng);
        }

        const auto r = fitEq (p, m, fs);
        printEq (r);

        const double before = focusError (p, m, r, fs, false);
        const double after  = focusError (p, m, r, fs, true);
        std::printf ("    Formfehler vorher %.2f dB, nachher %.2f dB\n", before, after);
        CHECK (after < before * 0.5, "Abweichung zur Zielkurve mindestens halbiert");

        bool notchFound = false;
        for (int b : { kRes1, kRes2 })
        {
            const auto& band = r.eq.bands[(size_t) b];
            if (band.enabled && std::abs (band.freq / 3200.0 - 1.0) < 0.05 && band.gainDb < -4.0)
                notchFound = true;
        }
        CHECK (notchFound, "Notch bei ~3,2 kHz gefunden");
        CHECK (eqResponseDb (r.eq, 1.0, 400.0, fs) < -2.5, "Mulm bei 400 Hz wird abgesenkt");
        CHECK (eqResponseDb (r.eq, 1.0, 7000.0, fs) < -2.0, "Fizz bei 7 kHz wird abgesenkt");

        bool spread = true;
        for (int a = kBell1; a <= kBell4; ++a)
            for (int b = a + 1; b <= kBell4; ++b)
                if (r.eq.bands[(size_t) a].enabled && r.eq.bands[(size_t) b].enabled
                    && std::abs (std::log2 (r.eq.bands[(size_t) a].freq / r.eq.bands[(size_t) b].freq)) < 0.45)
                    spread = false;
        CHECK (spread, "Keine zwei Glocken auf derselben Stelle");
    }

    // --- Test 1b: Gitarren-Box bei 630 Hz -----------------------------------
    {
        std::printf ("Test 1b: Rhythmus-Gitarre mit schmaler Box-Resonanz bei 630 Hz\n");
        const auto& p = profiles[0];
        std::vector<double> m ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double f = AnalysisGrid::centerHz (i);
            m[(size_t) i] = targetDbAt (p, f) - 30.0 + gaussLog (f, 630.0, 0.04, 8.0) + noise (rng);
        }
        const auto r = fitEq (p, m, fs);
        printEq (r);
        bool box = false;
        for (int b : { kRes1, kRes2 })
            box |= r.eq.bands[(size_t) b].enabled && std::abs (r.eq.bands[(size_t) b].freq / 630.0 - 1.0) < 0.06 && r.eq.bands[(size_t) b].gainDb < -3.0;
        CHECK (box, "Box-Resonanz bei 630 Hz wird mit einer Notch abgesenkt");
    }

    // --- Test 2: Signal entspricht bereits dem Ziel -> fast neutral ----------
    {
        std::printf ("Test 2: Signal entspricht schon der Zielkurve (Snare)\n");
        const auto& p = profiles[4];
        std::vector<double> m ((size_t) n);
        for (int i = 0; i < n; ++i)
            m[(size_t) i] = targetDbAt (p, AnalysisGrid::centerHz (i)) - 20.0;

        const auto r = fitEq (p, m, fs);
        printEq (r);

        double maxAbs = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double f = AnalysisGrid::centerHz (i);
            if (f > p.focusLo * 1.3 && f < p.focusHi / 1.3)
                maxAbs = std::max (maxAbs, std::abs (eqResponseDb (r.eq, 1.0, f, fs)
                                                     - bandResponseDb (r.eq.bands[kHpf], kHpf, 1.0, f, fs)
                                                     - bandResponseDb (r.eq.bands[kLpf], kLpf, 1.0, f, fs)));
        }
        std::printf ("    max. Eingriff ausser Cuts: %.2f dB\n", maxAbs);
        CHECK (maxAbs < 1.5, "Kaum Eingriff, wenn schon alles passt");
    }

    // --- Test 3: Alle Modi mit Rosa Rauschen und Grenzwerten -----------------
    {
        std::printf ("Test 3: alle Modi, Rosa Rauschen, 44.1 / 48 / 96 kHz\n");
        bool allFinite = true, inLimits = true;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const auto& p : profiles)
            {
                std::vector<double> m ((size_t) n);
                for (int i = 0; i < n; ++i)
                    m[(size_t) i] = -40.0 + noise (rng);

                const auto r = fitEq (p, m, rate);
                for (int b = 0; b < kNumBands; ++b)
                {
                    const auto& band = r.eq.bands[(size_t) b];
                    if (! std::isfinite (band.freq) || ! std::isfinite (band.gainDb) || ! std::isfinite (band.q))
                        allFinite = false;
                    if (band.enabled && (b != kRes1 && b != kRes2)
                        && (band.gainDb > p.maxBoostDb + 1e-9 || band.gainDb < p.maxCutDb - 1e-9))
                        inLimits = false;
                }
                if (! std::isfinite (r.outputTrimDb))
                    allFinite = false;
            }
        }
        CHECK (allFinite, "Alle Werte endlich");
        CHECK (inLimits, "Boost/Cut-Grenzen eingehalten");
    }

    // --- Test 4: Stille -> keine Glocken ------------------------------------
    {
        std::printf ("Test 4: fast nur Stille\n");
        std::vector<double> m ((size_t) n, -150.0);
        const auto r = fitEq (profiles[1], m, fs);
        bool anyBell = false;
        for (int b = kLowShelf; b <= kHighShelf; ++b)
            anyBell |= r.eq.bands[(size_t) b].enabled && std::abs (r.eq.bands[(size_t) b].gainDb) > 0.5;
        CHECK (! anyBell, "Keine Boosts aus dem Nichts");
    }

    // --- Test 5: Referenz-Abgleich -------------------------------------------
    {
        std::printf ("Test 5: Angleich an eine eigene Referenz\n");
        std::vector<double> ref ((size_t) n), cur ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double f = AnalysisGrid::centerHz (i);
            ref[(size_t) i] = targetDbAt (profiles[7], f) - 25.0 + gaussLog (f, 60.0, 0.6, 3.0);          // satter Bass
            cur[(size_t) i] = ref[(size_t) i] - 6.0 - gaussLog (f, 60.0, 0.6, 3.0) + gaussLog (f, 1000.0, 0.5, 2.5) // weniger Bass, boxiger
                            - gaussLog (f, 10000.0, 0.8, 3.0) + noise (rng) * 0.5;
        }
        const auto prof = makeReferenceProfile (ref);
        const auto r = fitEq (prof, cur, fs);
        printEq (r);
        double maxErr = 0.0;
        const auto sc = smoothDb (cur, 4), sr = smoothDb (ref, 4);
        double off = 0.0; int c = 0;
        std::vector<double> d ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double f = AnalysisGrid::centerHz (i);
            d[(size_t) i] = sc[(size_t) i] + eqResponseDb (r.eq, 1.0, f, fs) - sr[(size_t) i];
            if (f > 40 && f < 14000) { off += d[(size_t) i]; ++c; }
        }
        off /= c;
        for (int i = 0; i < n; ++i)
        {
            const double f = AnalysisGrid::centerHz (i);
            if (f > 40 && f < 14000) maxErr = std::max (maxErr, std::abs (d[(size_t) i] - off));
        }
        std::printf ("    max. Restabweichung zur Referenz: %.2f dB\n", maxErr);
        CHECK (maxErr < 1.5, "Klangbalance wird an die Referenz angeglichen");

        const double lRef = approxLoudnessDb (ref, nullptr, 1.0, fs);
        const double lCur = approxLoudnessDb (cur, &r.eq, 1.0, fs);
        std::printf ("    Lautheitsdifferenz nach EQ: %.2f dB (wird per Ausgang ausgeglichen)\n", lRef - lCur);
        CHECK (std::abs ((lRef - lCur) - 6.0) < 2.5, "Lautheitsdifferenz plausibel erkannt (ca. 6 dB leiser)");
    }

    std::printf (failures == 0 ? "\nAlle Tests bestanden.\n" : "\n%d Test(s) fehlgeschlagen.\n", failures);
    return failures == 0 ? 0 : 1;
}
