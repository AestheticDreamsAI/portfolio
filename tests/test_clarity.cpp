// Tests fuer die Klarheits-Stufe (psychoakustischer dynamischer EQ)
#include "../Source/DSP/Clarity.h"
#include "../Source/DSP/Dynamics.h"

#include <cstdio>
#include <random>

using namespace ironcurve;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else { std::printf ("  ok:   %s\n", msg); } } while (0)

// Leistung eines Frequenzbereichs per Goertzel (scharfe Bandgrenzen, Hann-Fenster) in dB
static double bandDb (const std::vector<float>& x, size_t start, size_t len, double fs, double lo, double hi)
{
    len = std::min (len, x.size() - start);
    double e = 0.0;
    for (double f = lo; f <= hi; f += 5.0)
    {
        const double w = 2.0 * kPi * f / fs, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) len);
            const double s0 = x[start + i] * win + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        e += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return 10.0 * std::log10 (e + 1e-20);
}

static double kLoudness (const std::vector<float>& l, const std::vector<float>& r, size_t start, double fs)
{
    KWeighting k; k.prepare (fs);
    KWeightedChannel cl, cr; cl.setup (k); cr.setup (k);
    std::vector<double> frames; double acc = 0; int n = 0; const int fl = (int) (0.1 * fs);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const float a = cl.process (l[i]), b = cr.process (r[i]);
        if (i < start) continue;
        acc += (double) a * a + (double) b * b;
        if (++n == fl) { frames.push_back (acc / n); acc = 0; n = 0; }
    }
    return gatedLoudness (frames);
}

int main()
{
    const double fs = 48000.0;
    const int len = (int) (6 * fs);
    std::mt19937 rng (5);
    std::normal_distribution<float> nd (0.0f, 1.0f);

    // Rosa-aehnliches Stereo-Rauschen
    auto pinkStereo = [&] (std::vector<float>& L, std::vector<float>& R, float gain)
    {
        float s[2][3] = {};
        L.resize ((size_t) len); R.resize ((size_t) len);
        for (int i = 0; i < len; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const float w = nd (rng);
                s[c][0] = 0.997f * s[c][0] + 0.029591f * w;
                s[c][1] = 0.985f * s[c][1] + 0.032534f * w;
                s[c][2] = 0.950f * s[c][2] + 0.048056f * w;
                (c == 0 ? L : R)[(size_t) i] = gain * (s[c][0] + s[c][1] + s[c][2] + 0.05f * w);
            }
    };

    auto run = [&] (double amount, const std::vector<float>& L, const std::vector<float>& R,
                    std::vector<float>& oL, std::vector<float>& oR, int& latency)
    {
        Clarity c; c.prepare (fs); c.setAmount (amount);
        latency = c.getLatencySamples();
        oL.assign (L.size(), 0.0f); oR.assign (R.size(), 0.0f);
        for (size_t i = 0; i < L.size(); ++i)
        {
            float l = L[i], r = R[i];
            c.process (l, r);
            oL[i] = l; oR[i] = r;
        }
    };

    // --- 1. Neutral bei 0 % : Signal bleibt identisch (nur verzoegert) ----------
    {
        std::printf ("Neutralitaet\n");
        std::vector<float> L, R, oL, oR; pinkStereo (L, R, 0.2f);
        int lat; run (0.0, L, R, oL, oR, lat);
        double maxErr = 0.0;
        for (size_t i = (size_t) lat + 4096; i < L.size(); ++i)
            maxErr = std::max ({ maxErr, (double) std::abs (oL[i] - L[i - (size_t) lat]), (double) std::abs (oR[i] - R[i - (size_t) lat]) });
        std::printf ("    Latenz %d Samples, max. Abweichung %.2e\n", lat, maxErr);
        CHECK (maxErr < 1e-4, "Bei 0 % ist das Signal unveraendert (perfekte Rekonstruktion)");
    }

    // --- 2. Tame: dominante Resonanz bei 300 Hz wird gezaehmt --------------------
    {
        std::printf ("Tame (Resonanz)\n");
        std::vector<float> L, R, oL, oR; pinkStereo (L, R, 0.15f);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.25f * (float) std::sin (2.0 * kPi * 300.0 * i / fs);
            L[(size_t) i] += s; R[(size_t) i] += s;
        }
        int lat; run (0.8, L, R, oL, oR, lat);
        const size_t st = (size_t) (2 * fs), n = (size_t) (3 * fs);
        const double before = bandDb (L, st, n, fs, 280, 320) - bandDb (L, st, n, fs, 600, 2000);
        const double after  = bandDb (oL, st + (size_t) lat, n, fs, 280, 320) - bandDb (oL, st + (size_t) lat, n, fs, 600, 2000);
        std::printf ("    300 Hz gegenueber Mitten: vorher %+.1f dB, nachher %+.1f dB\n", before, after);
        CHECK (before - after > 3.0, "Dominante Resonanz wird um mehr als 3 dB gezaehmt");
    }

    // --- 3. Recover: verdecktes Detail neben lautem Maskierer --------------------
    {
        std::printf ("Recover (Maskierung)\n");
        std::vector<float> L (len), R (len), oL, oR;
        // Lautes Rauschband 0,8-1,4 kHz (z. B. Gitarren-Mitten) und ein eigenstaendiges, 18 dB leiseres
        // Band 1,8-2,6 kHz (z. B. Praesenz der Stimme), das durch die Aufwaerts-Verdeckung teilweise untergeht.
        auto steep = [&] (double lo, double hi) {
            std::vector<Biquad> f (8);
            for (int k = 0; k < 4; ++k) { f[(size_t) k].c = makeCoeffs (FilterType::HighPass, lo, 0.707, 0, fs); f[(size_t) k + 4].c = makeCoeffs (FilterType::LowPass, hi, 0.707, 0, fs); }
            return f; };
        auto mF = steep (800, 1400), dF = steep (1800, 2600);
        for (int i = 0; i < len; ++i)
        {
            float a = nd (rng), b = nd (rng);
            for (auto& f : mF) a = f.process (a);
            for (auto& f : dF) b = f.process (b);
            const float x = 0.5f * a + 0.5f * 0.126f * b + 0.0005f * nd (rng);
            L[(size_t) i] = x; R[(size_t) i] = x;
        }
        int lat; run (0.8, L, R, oL, oR, lat);
        const size_t st = (size_t) (2 * fs), n = (size_t) (3 * fs);
        const double before = bandDb (L, st, n, fs, 2000, 2400) - bandDb (L, st, n, fs, 950, 1250);
        const double after  = bandDb (oL, st + (size_t) lat, n, fs, 2000, 2400) - bandDb (oL, st + (size_t) lat, n, fs, 950, 1250);
        std::printf ("    Leises Band gegenueber Maskierer: vorher %+.1f dB, nachher %+.1f dB\n", before, after);
        CHECK (after - before > 1.0, "Teilweise verdecktes Band wird hervorgeholt (> 1 dB)");
    }

    // --- 3b. Kante eines breiten Bereichs ist keine Resonanz -------------------
    {
        std::printf ("Keine Absenkung an Kanten\n");
        std::vector<float> L (len), R (len), oL, oR;
        std::vector<Biquad> lp (4);
        for (auto& f : lp) f.c = makeCoeffs (FilterType::LowPass, 2000, 0.707, 0, fs);
        for (int i = 0; i < len; ++i)
        {
            float a = nd (rng);
            for (auto& f : lp) a = f.process (a);
            L[(size_t) i] = R[(size_t) i] = 0.3f * a + 0.003f * nd (rng);
        }
        int lat; run (1.0, L, R, oL, oR, lat);
        const size_t st = (size_t) (2 * fs), n = (size_t) (3 * fs);
        const double before = bandDb (L, st, n, fs, 1700, 2100) - bandDb (L, st, n, fs, 500, 900);
        const double after  = bandDb (oL, st + (size_t) lat, n, fs, 1700, 2100) - bandDb (oL, st + (size_t) lat, n, fs, 500, 900);
        std::printf ("    Kante gegenueber Mitte: vorher %+.1f dB, nachher %+.1f dB\n", before, after);
        CHECK (after - before > -1.0, "Kante wird nicht als Resonanz abgesenkt");
    }

    // --- 4. Lautheit bleibt erhalten, Stereobild auch ----------------------------
    {
        std::printf ("Lautheit und Stereo\n");
        std::vector<float> L, R, oL, oR; pinkStereo (L, R, 0.15f);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.2f * (float) std::sin (2.0 * kPi * 180.0 * i / fs);
            L[(size_t) i] += s; R[(size_t) i] += 0.7f * s;
        }
        int lat; run (1.0, L, R, oL, oR, lat);
        const double lIn = kLoudness (L, R, (size_t) fs, fs), lOut = kLoudness (oL, oR, (size_t) fs + (size_t) lat, fs);
        std::printf ("    Lautheit vorher %.2f LUFS, nachher %.2f LUFS\n", lIn, lOut);
        CHECK (std::abs (lIn - lOut) < 1.0, "Wahrgenommene Lautheit bleibt erhalten (< 1 LU)");

    }

    // --- 5. Stereobild: Panorama bleibt exakt erhalten ---------------------------
    {
        std::printf ("Stereobild\n");
        std::vector<float> L, R, oL, oR; pinkStereo (L, R, 0.15f);
        for (int i = 0; i < len; ++i)
        {
            L[(size_t) i] += 0.2f * (float) std::sin (2.0 * kPi * 180.0 * i / fs);
            R[(size_t) i] = 0.5f * L[(size_t) i]; // halb links gepannt
        }
        int lat; run (1.0, L, R, oL, oR, lat);
        double maxDev = 0.0, maxL = 0.0;
        for (size_t i = (size_t) lat + 4096; i < oL.size(); ++i)
        {
            maxDev = std::max (maxDev, (double) std::abs (oR[i] - 0.5f * oL[i]));
            maxL = std::max (maxL, (double) std::abs (oL[i]));
        }
        std::printf ("    Abweichung vom Panorama: %.2e (Pegel %.2f)\n", maxDev, maxL);
        CHECK (maxDev < 1e-4, "Panorama bleibt exakt erhalten (beide Kanaele gleich bearbeitet)");
    }

    std::printf (failures == 0 ? "\nAlle Klarheits-Tests bestanden.\n" : "\n%d Test(s) fehlgeschlagen.\n", failures);
    return failures == 0 ? 0 : 1;
}
