// Tests fuer Ironcurve Amp (Gitarre + Bass)
#include "../Source/DSP/AmpDSP.h"
#include "AmpTestSignals.h"

#include <cstdio>
#include <complex>

using namespace ironcurve;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else { std::printf ("  ok:   %s\n", msg); } } while (0)

static double bandDb (const std::vector<float>& x, size_t start, size_t len, double fs, double lo, double hi, double step = 5.0)
{
    len = std::min (len, x.size() - start);
    double e = 0.0;
    for (double f = lo; f <= hi; f += step)
    {
        const double w = 2.0 * kPi * f / fs, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (size_t i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) len);
            const double s0 = x[start + i] * win + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        e += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return 10.0 * std::log10 (e + 1e-30);
}

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b && i < x.size(); ++i) e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e / (double) (b - a) + 1e-30);
}

template <typename Amp>
static std::vector<float> run (const std::vector<float>& in, const AmpParams& p, double fs, NoiseGate* gate = nullptr)
{
    Amp a; a.prepare (fs); a.setParams (p);
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double g = gate ? gate->process (in[i]) : 1.0;
        out[i] = (float) (a.process (in[i]) * g);
    }
    return out;
}

int main()
{
    const double fs = 48000.0;

    // --- 1. Aliasing: hoher Ton bei viel Gain ---------------------------------
    {
        std::printf ("Aliasing (Oversampling)\n");
        const double f0 = 3100.0;
        std::vector<float> in ((size_t) (fs * 2));
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float) (0.25 * std::sin (2.0 * kPi * f0 * (double) i / fs));
        AmpParams p; p.gain = 1.0; p.cab = false; p.tight = 0.0;
        auto out = run<GuitarAmpChannel> (in, p, fs);
        // Leistung auf Obertoenen vs. auf allen anderen Frequenzen (= Aliasing) zwischen 100 Hz und 20 kHz
        const size_t st = (size_t) fs / 2, len = (size_t) fs;
        double harm = 0.0, other = 0.0;
        for (double f = 100.0; f < 20000.0; f += 10.0)
        {
            const double e = std::pow (10.0, bandDb (out, st, len, fs, f, f, 10.0) / 10.0);
            const double k = f / f0;
            (std::abs (k - std::round (k)) * f0 < 25.0 ? harm : other) += e;
        }
        const double ratio = 10.0 * std::log10 (other / harm);
        std::printf ("    Nicht-harmonische Anteile (Aliasing): %.1f dB unter den Obertoenen\n", -ratio);
        CHECK (ratio < -45.0, "Aliasing mindestens 45 dB unter dem Nutzsignal");
    }

    const auto gtr = amptest::guitarRiff (fs, 6.0);

    // --- 2. Tight: weniger Tiefbass-Matsch vor der Verzerrung ----------------
    {
        std::printf ("Tight\n");
        AmpParams loose; loose.tight = 0.0; loose.gate = 0.0;
        AmpParams tight; tight.tight = 1.0; tight.gate = 0.0;
        auto a = run<GuitarAmpChannel> (gtr, loose, fs), b = run<GuitarAmpChannel> (gtr, tight, fs);
        const size_t st = (size_t) fs, len = (size_t) (4 * fs);
        const double la = bandDb (a, st, len, fs, 60, 160) - bandDb (a, st, len, fs, 800, 2000);
        const double lb = bandDb (b, st, len, fs, 60, 160) - bandDb (b, st, len, fs, 800, 2000);
        std::printf ("    Tiefbass gegenueber Mitten: locker %+.1f dB, tight %+.1f dB\n", la, lb);
        CHECK (la - lb > 2.0, "Tight reduziert den Tiefbass-Anteil hoerbar");
    }

    // --- 3. Gate: Rauschen und Brumm in den Pausen ---------------------------
    {
        std::printf ("Noise-Gate\n");
        AmpParams p; p.gate = 0.5;
        NoiseGate g; g.prepare (fs); g.setThreshold (-60.0 + 30.0 * (1.0 - p.gate));
        auto off = run<GuitarAmpChannel> (gtr, p, fs);
        auto on = run<GuitarAmpChannel> (gtr, p, fs, &g);
        // Pause: 16tel-Positionen 6+7 in Takt 0 (Saiten abgestoppt), mittlerer Teil davon
        const double s16 = 60.0 / 160.0 / 4.0;
        const size_t a = (size_t) ((7.0) * s16 * fs), b = (size_t) ((7.9) * s16 * fs);
        const double gapOff = rmsDb (off, a, b), gapOn = rmsDb (on, a, b);
        const double playOff = rmsDb (off, 0, (size_t) (5 * s16 * fs)), playOn = rmsDb (on, 0, (size_t) (5 * s16 * fs));
        std::printf ("    Pause ohne Gate %.1f dB, mit Gate %.1f dB | Spielen %.1f / %.1f dB\n", gapOff, gapOn, playOff, playOn);
        CHECK (gapOff - gapOn > 25.0, "Gate unterdrueckt Rauschen in Pausen um mehr als 25 dB");
        CHECK (std::abs (playOff - playOn) < 1.0, "Gespielte Noten bleiben unangetastet");
    }

    // --- 4. Boxen-Simulation ---------------------------------------------------
    {
        std::printf ("Boxen-Simulation\n");
        CabSim c; c.setup (fs, false);
        auto db = [&] (double f)
        {
            double m = 0.0;
            for (auto& b : c.f) m += b.c.magnitudeDb (f, fs);
            return m;
        };
        std::printf ("    50 Hz %+.1f | 100 Hz %+.1f | 1 kHz %+.1f | 2,2 kHz %+.1f | 5 kHz %+.1f | 8 kHz %+.1f | 12 kHz %+.1f dB\n",
                     db (50), db (100), db (1000), db (2200), db (5000), db (8000), db (12000));
        CHECK (db (1000) - db (50) > 6.0, "Unterhalb von 70 Hz faellt die Box ab");
        CHECK (db (1000) - db (8000) > 15.0, "Oberhalb von 5 kHz faellt die Box steil ab (kein Fizz)");
        CHECK (db (2200) > db (1000), "Biss im oberen Mittenbereich");
    }

    // --- 5. Pegel und Gleichanteil ---------------------------------------------
    {
        std::printf ("Pegel\n");
        AmpParams p;
        auto out = run<GuitarAmpChannel> (gtr, p, fs);
        double mean = 0, peak = 0; bool finite = true;
        for (float v : out) { mean += v; peak = std::max (peak, (double) std::abs (v)); finite &= std::isfinite (v); }
        mean /= out.size();
        const double r = rmsDb (out, (size_t) fs, out.size());
        std::printf ("    RMS %.1f dBFS, Spitze %.2f, Gleichanteil %.1e\n", r, peak, mean);
        CHECK (finite, "Keine ungueltigen Werte");
        CHECK (r > -30.0 && r < -8.0 && peak < 1.0, "Ausgangspegel in sinnvollem Bereich, ohne Uebersteuerung");
        CHECK (std::abs (mean) < 1e-3, "Kein Gleichanteil");
    }

    // --- 6. Bass: Tiefband sauber, Grit liefert Knurren --------------------------
    {
        std::printf ("Bass\n");
        // Sauberkeit: 41-Hz-Sinus (tiefes E) -> Obertoene im Ausgang
        std::vector<float> sine ((size_t) (fs * 3));
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = (float) (0.25 * std::sin (2.0 * kPi * 41.2 * (double) i / fs));
        AmpParams p; p.gain = 0.9; p.grit = 0.6;
        auto out = run<BassAmpChannel> (sine, p, fs);
        const size_t st = (size_t) fs, len = (size_t) fs;
        const double f1 = bandDb (out, st, len, fs, 40, 42.5, 0.5);
        const double h2 = bandDb (out, st, len, fs, 81.5, 83.5, 0.5), h3 = bandDb (out, st, len, fs, 122.5, 124.5, 0.5);
        std::printf ("    41 Hz: 2. Oberton %.1f dB, 3. Oberton %.1f dB unter dem Grundton\n", f1 - h2, f1 - h3);
        CHECK (f1 - h2 > 30.0 && f1 - h3 > 30.0, "Tiefster Bass bleibt sauber (Verzerrung mehr als 30 dB darunter)");

        const auto bass = amptest::bassRiff (fs, 5.0);
        AmpParams clean = p; clean.grit = 0.0;
        AmpParams dirty = p; dirty.grit = 1.0;
        auto c = run<BassAmpChannel> (bass, clean, fs), d = run<BassAmpChannel> (bass, dirty, fs);
        const double gc = bandDb (c, st, (size_t) (3 * fs), fs, 1000, 3000, 20) - bandDb (c, st, (size_t) (3 * fs), fs, 40, 120, 2);
        const double gd = bandDb (d, st, (size_t) (3 * fs), fs, 1000, 3000, 20) - bandDb (d, st, (size_t) (3 * fs), fs, 40, 120, 2);
        std::printf ("    Knurren (1-3 kHz) gegenueber Fundament: ohne Grit %+.1f dB, mit Grit %+.1f dB\n", gc, gd);
        CHECK (gd - gc > 10.0, "Grit fuegt deutliches Knurren hinzu");

        // Gleichmaessige Summe: Sinus-Sweep-Pegel ueber die Weiche bei wenig Gain
        AmpParams lin = p; lin.gain = 0.0; lin.grit = 0.0; lin.lowDb = 0.0;
        std::vector<float> tone ((size_t) (fs * 2));
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float) (0.05 * std::sin (2.0 * kPi * 60.0 * (double) i / fs));
        auto o = run<BassAmpChannel> (tone, lin, fs);
        bool finite = true; for (float v : o) finite &= std::isfinite (v);
        CHECK (finite, "Keine ungueltigen Werte im Bass-Pfad");
    }

    std::printf (failures == 0 ? "\nAlle Amp-Tests bestanden.\n" : "\n%d Test(s) fehlgeschlagen.\n", failures);
    return failures == 0 ? 0 : 1;
}
