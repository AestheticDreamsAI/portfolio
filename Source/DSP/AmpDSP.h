// Ironcurve Amp - eigener High-Gain-Verstaerker fuer Gitarre und Bass (reines C++)
//
// Aufbau nach gaengiger Praxis fuer Modern Metal (eigene Umsetzung, keine Nachbildung eines Produkts):
//
// GITARRE
//   Noise-Gate (Detektor am sauberen Eingang, wirkt am Ausgang)
//   -> "Tight"-Vorstufe wie ein Overdrive-Boost vor dem Amp: Low-Cut, Mitten-Buckel, leichte Hoehenbedaempfung
//   -> 4x Oversampling
//   -> 4 kaskadierte Roehrenstufen (asymmetrische Kennlinie, Koppel-Hochpass und Miller-Tiefpass je Stufe)
//   -> Klangregelung (Bass / Mitten / Hoehen), Endstufe mit Presence/Depth und Saettigung mit "Sag"
//   -> Boxen-Simulation 4x12 (Lautsprecherbereich ca. 70 Hz - 5 kHz, Resonanz um 75 Hz)
//
// BASS
//   Frequenzweiche (Linkwitz-Riley 4. Ordnung): unten sauber + leicht komprimiert, oben verzerrt
//   -> Summe, phasenrichtig (unterer Zweig um die Oversampling-Latenz verzoegert)
#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ironcurve
{

// ---------------------------------------------------------------------------
// 4x-Oversampler mit gefensterten Sinc-FIR-Filtern (65 Taps, Kaiser), ganzzahlige Latenz
class Oversampler4x
{
public:
    static constexpr int kTaps = 65;     // ungerade -> Latenz ganzzahlig
    static constexpr int kFactor = 4;

    Oversampler4x()
    {
        // Tiefpass bei 0,22 * Hochrate (knapp unter der Basis-Nyquist)
        const double fc = 0.5 / kFactor * 0.9, beta = 8.0;
        double sum = 0.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double m = n - (kTaps - 1) / 2.0;
            const double sinc = std::abs (m) < 1e-12 ? 2.0 * fc : std::sin (2.0 * kPi * fc * m) / (kPi * m);
            const double r = 2.0 * n / (kTaps - 1) - 1.0;
            h[(size_t) n] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            sum += h[(size_t) n];
        }
        for (auto& v : h) v /= sum;
        reset();
    }

    // Latenz in Basis-Samples: (Taps-1)/2 hoch + (Taps-1)/2 runter, geteilt durch 4
    static constexpr int latency() { return (kTaps - 1) / kFactor; }

    void reset()
    {
        upHist.fill (0.0);
        downHist.fill (0.0);
        upPos = downPos = 0;
    }

    // Ein Basis-Sample -> 4 Hochraten-Samples
    void up (double x, double* out)
    {
        for (int p = 0; p < kFactor; ++p)
        {
            upHist[(size_t) upPos] = p == 0 ? x * kFactor : 0.0; // Nullen einfuegen, Pegel ausgleichen
            upPos = (upPos + 1) % kTaps;
            double y = 0.0;
            // Polyphasig waere schneller; bei 65 Taps ist die direkte Form ausreichend
            for (int k = 0; k < kTaps; ++k)
                y += h[(size_t) k] * upHist[(size_t) ((upPos + kTaps - 1 - k) % kTaps)];
            out[p] = y;
        }
    }

    // 4 Hochraten-Samples -> ein Basis-Sample
    double down (const double* in)
    {
        double y = 0.0;
        for (int p = 0; p < kFactor; ++p)
        {
            downHist[(size_t) downPos] = in[p];
            downPos = (downPos + 1) % kTaps;
        }
        for (int k = 0; k < kTaps; ++k)
            y += h[(size_t) k] * downHist[(size_t) ((downPos + kTaps - 1 - k) % kTaps)];
        return y;
    }

private:
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    std::array<double, kTaps> h {};
    std::array<double, kTaps> upHist {}, downHist {};
    int upPos = 0, downPos = 0;
};

// ---------------------------------------------------------------------------
// Roehrenstufe: Verstaerkung -> asymmetrische Kennlinie (Bias = gerade Obertoene) -> Koppel-HP + Miller-TP
struct TubeStage
{
    Biquad hp, lp;
    double bias = 0.2, gain = 1.0;

    void setup (double fsHigh, double hpHz, double lpHz, double b)
    {
        hp.c = makeCoeffs (FilterType::HighPass, hpHz, 0.707, 0.0, fsHigh);
        lp.c = makeCoeffs (FilterType::LowPass, lpHz, 0.707, 0.0, fsHigh);
        bias = b;
        hp.reset(); lp.reset();
    }

    inline double process (double x) noexcept
    {
        const double v = x * gain + bias;
        // Gitter-Strom-artige weiche Begrenzung oben, haertere unten (Cutoff)
        double y = v > 0.0 ? std::tanh (v) : std::tanh (1.6 * v) / 1.6;
        y -= (bias > 0.0 ? std::tanh (bias) : std::tanh (1.6 * bias) / 1.6); // Arbeitspunkt abziehen
        return lp.process ((float) hp.process ((float) y));
    }
};

// ---------------------------------------------------------------------------
class NoiseGate
{
public:
    void prepare (double fs)
    {
        envAtt = std::exp (-1.0 / (0.0005 * fs));
        envRel = std::exp (-1.0 / (0.006 * fs));   // schnell: Metal-Riffs haben kurze Pausen
        gOpen = std::exp (-1.0 / (0.0007 * fs));
        gClose = std::exp (-1.0 / (0.020 * fs));
        env = 0.0; g = 0.0; open = false;
    }
    void setThreshold (double db) { thrOpen = std::pow (10.0, db / 20.0); thrClose = thrOpen * 0.5; } // 6 dB Hysterese
    void setEnabled (bool e) { enabled = e; }

    // Detektor-Sample (sauberer Eingang) -> Gate-Faktor
    inline double process (double detector) noexcept
    {
        if (! enabled) return 1.0;
        const double a = std::abs (detector);
        env = a > env ? envAtt * env + (1 - envAtt) * a : envRel * env + (1 - envRel) * a;
        if (! open && env > thrOpen) open = true;
        else if (open && env < thrClose) open = false;
        const double target = open ? 1.0 : 0.0;
        const double c = target > g ? gOpen : gClose;
        g = c * g + (1 - c) * target;
        return g;
    }

private:
    double envAtt = 0, envRel = 0, gOpen = 0, gClose = 0, env = 0, g = 0, thrOpen = 0.002, thrClose = 0.001;
    bool open = false, enabled = true;
};

// ---------------------------------------------------------------------------
// Boxen-Simulation 4x12 (an Datenblatt-Eckwerten orientiert: 70 Hz - 5 kHz, Resonanz ~75 Hz)
struct CabSim
{
    std::array<Biquad, 8> f;
    void setup (double fs, bool bass)
    {
        if (! bass)
        {
            f[0].c = makeCoeffs (FilterType::HighPass, 70.0, 1.1, 0.0, fs);       // Resonanz-Hochpass
            f[1].c = makeCoeffs (FilterType::HighPass, 60.0, 0.707, 0.0, fs);
            f[2].c = makeCoeffs (FilterType::Peak, 110.0, 1.0, 2.5, fs);          // Boxen-Druck
            f[3].c = makeCoeffs (FilterType::Peak, 450.0, 0.8, -3.0, fs);         // typische Senke 4x12 + Mikrofon
            f[4].c = makeCoeffs (FilterType::Peak, 2200.0, 1.0, 3.5, fs);         // Biss
            f[5].c = makeCoeffs (FilterType::Peak, 3800.0, 2.0, 2.0, fs);         // Kegel-Spitze
            f[6].c = makeCoeffs (FilterType::LowPass, 5000.0, 0.9, 0.0, fs);      // Lautsprecher-Ende (~5 kHz)
            f[7].c = makeCoeffs (FilterType::LowPass, 5600.0, 0.7, 0.0, fs);      // steiler Abfall: kein Fizz
        }
        else
        {
            f[0].c = makeCoeffs (FilterType::HighPass, 40.0, 0.707, 0.0, fs);
            f[1].c = makeCoeffs (FilterType::Peak, 800.0, 0.7, 1.5, fs);
            f[2].c = makeCoeffs (FilterType::Peak, 1800.0, 1.0, 2.5, fs);         // Knurren
            f[3].c = makeCoeffs (FilterType::LowPass, 4500.0, 0.8, 0.0, fs);
            f[4].c = makeCoeffs (FilterType::LowPass, 6500.0, 0.6, 0.0, fs);
            for (int i = 5; i < 8; ++i) f[(size_t) i].c = BiquadCoeffs {};
        }
        for (auto& b : f) b.reset();
    }
    inline double process (double x) noexcept
    {
        float y = (float) x;
        for (auto& b : f) y = b.process (y);
        return y;
    }
};

// ---------------------------------------------------------------------------
struct AmpParams
{
    double inputDb = 0.0;
    double gate = 0.5;       // 0 = aus, 1 = sehr streng
    double tight = 0.6;      // 0..1
    double gain = 0.65;      // 0..1
    double bass = 0.5, mid = 0.5, treble = 0.5, presence = 0.5, depth = 0.5; // 0..1, 0.5 = neutral
    double levelDb = 0.0;
    bool cab = true;
    // Bass-Modus
    double crossoverHz = 200.0;
    double lowDb = 0.0;      // Pegel des sauberen Tiefbands
    double grit = 0.6;       // Anteil des verzerrten Bands (0..1)
};

// ---------------------------------------------------------------------------
class GuitarAmpChannel
{
public:
    void prepare (double fs)
    {
        sr = fs;
        const double fh = fs * Oversampler4x::kFactor;
        os.reset();
        fsHigh = fh;
        setupStages (0.6);
        dcHp.c = makeCoeffs (FilterType::HighPass, 15.0, 0.707, 0.0, fs);
        cab.setup (fs, false);
        sagEnv = 0.0;
        sagCoef = std::exp (-1.0 / (0.050 * fs));
        lastTight = lastTone = -1.0;
    }

    void setParams (const AmpParams& p)
    {
        // Tight-Vorstufe (wie Overdrive-Boost mit Drive auf 0): Low-Cut 70..220 Hz, Mitten-Buckel, Hoehen leicht weg
        if (p.tight != lastTight)
        {
            lastTight = p.tight;
            setupStages (p.tight);
            const double hpf = 70.0 * std::pow (220.0 / 70.0, p.tight);
            pre[0].c = makeCoeffs (FilterType::HighPass, hpf, 0.707, 0.0, sr);
            pre[1].c = makeCoeffs (FilterType::HighPass, hpf, 0.707, 0.0, sr);
            pre[2].c = makeCoeffs (FilterType::Peak, 800.0, 0.7, 2.0 + 3.0 * p.tight, sr);
            pre[3].c = makeCoeffs (FilterType::LowPass, 9000.0, 0.707, 0.0, sr);
        }
        const double toneKey = p.bass * 1e6 + p.mid * 1e4 + p.treble * 1e2 + p.presence + p.depth * 1e-2;
        if (toneKey != lastTone)
        {
            lastTone = toneKey;
            auto db = [] (double v, double range) { return (v - 0.5) * 2.0 * range; };
            tone[0].c = makeCoeffs (FilterType::LowShelf, 120.0, 0.7, db (p.bass, 10.0), sr);
            tone[1].c = makeCoeffs (FilterType::Peak, 650.0, 0.8, db (p.mid, 10.0), sr);
            tone[2].c = makeCoeffs (FilterType::HighShelf, 2800.0, 0.7, db (p.treble, 10.0), sr);
            tone[3].c = makeCoeffs (FilterType::HighShelf, 4500.0, 0.7, db (p.presence, 8.0), sr);
            tone[4].c = makeCoeffs (FilterType::Peak, 90.0, 1.0, db (p.depth, 8.0), sr);
        }
        // Gain: Gesamtverstaerkung ~6..58 dB, verteilt auf die Stufen
        const double totalDb = 6.0 + 52.0 * p.gain;
        const double per = totalDb / 4.0;
        for (auto& s : stages) s.gain = std::pow (10.0, per / 20.0);
        inGain = std::pow (10.0, p.inputDb / 20.0) * 4.0; // DI liegt typisch bei -18 dBFS
        outGain = std::pow (10.0, p.levelDb / 20.0);
        useCab = p.cab;
    }

    inline double process (double x) noexcept
    {
        double v = x * inGain;
        for (auto& b : pre) v = b.process ((float) v);

        double hi[Oversampler4x::kFactor];
        os.up (v, hi);
        for (double& s : hi)
            for (auto& st : stages)
                s = st.process (s);
        v = os.down (hi);

        for (auto& b : tone) v = b.process ((float) v);

        // Endstufe: Saettigung mit "Sag" (Netzteil gibt bei lauten Passagen etwas nach)
        sagEnv = sagCoef * sagEnv + (1 - sagCoef) * std::abs (v);
        const double sag = 1.0 / (1.0 + 0.6 * sagEnv);
        v = std::tanh (v * 0.8 * sag) / 0.8;

        if (useCab) v = cab.process (v);
        v = dcHp.process ((float) v);
        return v * outGain * 0.35;
    }

    static constexpr int latency() { return Oversampler4x::latency(); }

private:
    // Koppel-Hochpaesse zwischen den Stufen: "Tight" macht sie hoeher (wie ein Tight-Schalter am Amp)
    void setupStages (double t)
    {
        const double g[4] = { stages[0].gain, stages[1].gain, stages[2].gain, stages[3].gain };
        stages[0].setup (fsHigh, 20.0, 12000.0, 0.15);
        stages[1].setup (fsHigh, 30.0 + 50.0 * t, 9000.0, 0.25);
        stages[2].setup (fsHigh, 40.0 + 110.0 * t, 7500.0, 0.20);
        stages[3].setup (fsHigh, 50.0 + 130.0 * t, 6500.0, 0.30);
        for (int i = 0; i < 4; ++i) stages[(size_t) i].gain = g[i];
    }

    double sr = 48000.0, fsHigh = 192000.0;
    Oversampler4x os;
    std::array<TubeStage, 4> stages;
    std::array<Biquad, 4> pre;
    std::array<Biquad, 5> tone;
    Biquad dcHp;
    CabSim cab;
    double inGain = 1.0, outGain = 1.0, sagEnv = 0.0, sagCoef = 0.0;
    double lastTight = -1.0, lastTone = -1.0;
    bool useCab = true;
};

// ---------------------------------------------------------------------------
class BassAmpChannel
{
public:
    void prepare (double fs)
    {
        sr = fs;
        const double fh = fs * Oversampler4x::kFactor;
        os.reset();
        stages[0].setup (fh, 60.0, 10000.0, 0.20);
        stages[1].setup (fh, 90.0, 8000.0, 0.25);
        cab.setup (fs, true);
        delay.assign ((size_t) Oversampler4x::latency() + 1, 0.0);
        dPos = 0;
        compEnv = 0.0;
        compAtt = std::exp (-1.0 / (0.005 * fs));
        compRel = std::exp (-1.0 / (0.120 * fs));
        lastX = -1.0; lastTone = -1.0;
    }

    void setParams (const AmpParams& p)
    {
        if (p.crossoverHz != lastX)
        {
            lastX = p.crossoverHz;
            // Linkwitz-Riley 4. Ordnung = zwei Butterworth 2. Ordnung in Reihe (Summe phasengleich)
            for (int i = 0; i < 2; ++i)
            {
                lp[i].c = makeCoeffs (FilterType::LowPass, p.crossoverHz, 0.707, 0.0, sr);
                hp[i].c = makeCoeffs (FilterType::HighPass, p.crossoverHz, 0.707, 0.0, sr);
            }
        }
        const double toneKey = p.bass * 1e4 + p.mid * 1e2 + p.treble;
        if (toneKey != lastTone)
        {
            lastTone = toneKey;
            auto db = [] (double v, double range) { return (v - 0.5) * 2.0 * range; };
            tone[0].c = makeCoeffs (FilterType::Peak, 500.0, 0.8, db (p.bass, 8.0), sr);   // Koerper des Grit-Bands
            tone[1].c = makeCoeffs (FilterType::Peak, 1200.0, 0.9, db (p.mid, 10.0), sr);  // Knurren
            tone[2].c = makeCoeffs (FilterType::HighShelf, 3000.0, 0.7, db (p.treble, 10.0), sr);
        }
        const double totalDb = 4.0 + 40.0 * p.gain;
        for (auto& s : stages) s.gain = std::pow (10.0, totalDb / 2.0 / 20.0);
        inGain = std::pow (10.0, p.inputDb / 20.0) * 4.0;
        lowGain = std::pow (10.0, p.lowDb / 20.0);
        grit = p.grit;
        outGain = std::pow (10.0, p.levelDb / 20.0);
        useCab = p.cab;
    }

    inline double process (double x) noexcept
    {
        const double in = x * inGain;

        // Tiefband: sauber, sanft komprimiert (gleichmaessiges Fundament)
        double low = in;
        for (auto& b : lp) low = b.process ((float) low);
        const double a = std::abs (low);
        compEnv = a > compEnv ? compAtt * compEnv + (1 - compAtt) * a : compRel * compEnv + (1 - compRel) * a;
        const double over = std::max (0.0, 20.0 * std::log10 (compEnv + 1e-9) - (-12.0));
        low *= std::pow (10.0, -over * (1.0 - 1.0 / 3.0) / 20.0) * 1.6; // 3:1, Ausgleich

        // Phasengleich zum verzoegerten Hochband (Oversampling-Latenz)
        delay[(size_t) dPos] = low;
        dPos = (dPos + 1) % (int) delay.size();
        const double lowDelayed = delay[(size_t) dPos];

        // Hochband: verzerrt
        double hiB = in;
        for (auto& b : hp) hiB = b.process ((float) hiB);
        double hs[Oversampler4x::kFactor];
        os.up (hiB, hs);
        for (double& s : hs)
            for (auto& st : stages)
                s = st.process (s);
        hiB = os.down (hs);
        for (auto& b : tone) hiB = b.process ((float) hiB);
        if (useCab) hiB = cab.process (hiB);

        return (lowDelayed * lowGain * 0.5 + hiB * grit * 0.6) * outGain;
    }

private:
    double sr = 48000.0;
    Oversampler4x os;
    std::array<TubeStage, 2> stages;
    std::array<Biquad, 2> lp, hp;
    std::array<Biquad, 3> tone;
    CabSim cab;
    std::vector<double> delay;
    int dPos = 0;
    double inGain = 1.0, lowGain = 1.0, grit = 0.6, outGain = 1.0;
    double compEnv = 0.0, compAtt = 0.0, compRel = 0.0;
    double lastX = -1.0, lastTone = -1.0;
    bool useCab = true;
};

} // namespace ironcurve
