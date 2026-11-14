// Ironcurve Master - Dynamik-Bausteine (reines C++, testbar ohne JUCE)
//
//  KWeighting      ITU-R BS.1770 K-Filter (exakte Koeffizienten fuer jede Samplerate)
//  Compressor      Feed-forward, stereo-gekoppelt, Soft-Knee ("Glue")
//  DeHarsher       Dynamischer EQ: senkt ein Band nur, wenn es im Verhaeltnis zum Gesamtsignal zu laut wird
//  Limiter         Lookahead-Brickwall (Hold + Release + Boxcar), garantiert keine Overs
//  StereoShaper    Mid/Side: Bass mono, Breite oberhalb
#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ironcurve
{

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g)  { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

// ---------------------------------------------------------------------------
struct KWeighting
{
    BiquadCoeffs shelf, highpass;

    void prepare (double fs)
    {
        // Koeffizienten-Formeln wie in libebur128 (gelten fuer jede Samplerate)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highpass.b0 = 1.0; highpass.b1 = -2.0; highpass.b2 = 1.0;
            highpass.a1 = 2.0 * (K * K - 1.0) / a0;
            highpass.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
};

// Pro Kanal ein K-gefilterter Zustand
struct KWeightedChannel
{
    Biquad s1, s2;
    void setup (const KWeighting& k) { s1.c = k.shelf; s2.c = k.highpass; s1.reset(); s2.reset(); }
    inline float process (float x) noexcept { return s2.process (s1.process (x)); }
};

// Integrierte Lautheit nach BS.1770 aus 100-ms-Frames (mittleres Quadrat, Kanaele summiert)
inline double gatedLoudness (const std::vector<double>& frameMs)
{
    // 400-ms-Bloecke mit 75 % Overlap = 4 aufeinanderfolgende Frames
    std::vector<double> blocks;
    for (size_t i = 0; i + 3 < frameMs.size(); ++i)
        blocks.push_back ((frameMs[i] + frameMs[i + 1] + frameMs[i + 2] + frameMs[i + 3]) * 0.25);

    auto loud = [] (double ms) { return -0.691 + 10.0 * std::log10 (std::max (ms, 1.0e-20)); };

    double sum = 0.0; int n = 0;
    for (double b : blocks)
        if (loud (b) > -70.0) { sum += b; ++n; }
    if (n == 0)
        return -100.0;

    const double relGate = loud (sum / n) - 10.0;
    sum = 0.0; n = 0;
    for (double b : blocks)
        if (loud (b) > -70.0 && loud (b) > relGate) { sum += b; ++n; }

    return n > 0 ? loud (sum / n) : -100.0;
}

// ---------------------------------------------------------------------------
class Compressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        detCoef = std::exp (-1.0 / (0.010 * fs));
        attCoef = std::exp (-1.0 / (0.020 * fs));
        relCoef = std::exp (-1.0 / (0.200 * fs));
        env = 0.0;
        grDb = 0.0;
    }

    void setParams (double thresholdDb, double ratio, double kneeDb = 6.0)
    {
        thr = thresholdDb;
        slope = 1.0 - 1.0 / std::max (1.0, ratio);
        knee = kneeDb;
    }

    // Detektor-Pegel (dB RMS) des zuletzt verarbeiteten Samples
    double lastLevelDb() const { return 10.0 * std::log10 (env + 1.0e-20); }
    double currentGainReductionDb() const { return grDb; }

    // Liefert den Gain (linear) fuer ein Stereo-Sample
    inline double process (float l, float r) noexcept
    {
        const double ms = 0.5 * ((double) l * l + (double) r * r);
        env = detCoef * env + (1.0 - detCoef) * ms;
        const double level = 10.0 * std::log10 (env + 1.0e-20);

        double over = level - thr;
        double target;
        if (over <= -knee * 0.5)      target = 0.0;
        else if (over >= knee * 0.5)  target = over * slope;
        else                          target = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);

        const double c = target > grDb ? attCoef : relCoef;
        grDb = c * grDb + (1.0 - c) * target;
        return dbToGain (-grDb);
    }

private:
    double fs = 48000.0, detCoef = 0, attCoef = 0, relCoef = 0;
    double env = 0.0, grDb = 0.0;
    double thr = 0.0, slope = 0.0, knee = 6.0;
};

// ---------------------------------------------------------------------------
class DeHarsher
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        envCoef = std::exp (-1.0 / (0.015 * fs));
        attCoef = std::exp (-1.0 / (0.005 * fs));
        relCoef = std::exp (-1.0 / (0.120 * fs));
        envBand = envFull = 1.0e-12;
        grDb = 0.0;
        for (auto& f : eq) f.reset();
        detector.reset();
        setFrequency (freq);
        counter = 0;
    }

    void setFrequency (double hz)
    {
        freq = hz;
        // Bandpass (0 dB Peak) fuer die Erkennung
        const double w0 = 2.0 * kPi * freq / fs, alpha = std::sin (w0) / (2.0 * kQ);
        const double a0 = 1.0 + alpha;
        detector.c.b0 = alpha / a0; detector.c.b1 = 0.0; detector.c.b2 = -alpha / a0;
        detector.c.a1 = -2.0 * std::cos (w0) / a0; detector.c.a2 = (1.0 - alpha) / a0;
        updateEq();
    }

    // thresholdDb: erlaubtes Verhaeltnis Band/Gesamt; maxCutDb: maximale Absenkung (positiv)
    void setParams (double thresholdDb, double maxCutDb) { thr = thresholdDb; maxCut = std::max (0.0, maxCutDb); }

    double lastRatioDb() const { return 10.0 * std::log10 (envBand / envFull); }
    double currentGainReductionDb() const { return grDb; }

    // In-place Stereo-Verarbeitung
    inline void process (float& l, float& r) noexcept
    {
        const float mid = 0.5f * (l + r);
        const double b = detector.process (mid);
        envBand = envCoef * envBand + (1.0 - envCoef) * b * b + 1.0e-14;
        envFull = envCoef * envFull + (1.0 - envCoef) * (double) mid * mid + 1.0e-12;

        const double ratioDb = 10.0 * std::log10 (envBand / envFull);
        const double target = std::clamp ((ratioDb - thr) * 0.8, 0.0, maxCut);
        const double c = target > grDb ? attCoef : relCoef;
        grDb = c * grDb + (1.0 - c) * target;

        if (++counter >= 16)
        {
            counter = 0;
            updateEq();
        }

        l = eq[0].process (l);
        r = eq[1].process (r);
    }

private:
    void updateEq()
    {
        const auto c = makeCoeffs (FilterType::Peak, freq, kQ, -grDb, fs);
        eq[0].c = c;
        eq[1].c = c;
    }

    static constexpr double kQ = 1.4;
    double fs = 48000.0, freq = 3800.0;
    double envCoef = 0, attCoef = 0, relCoef = 0;
    double envBand = 1e-12, envFull = 1e-12, grDb = 0.0;
    double thr = 0.0, maxCut = 0.0;
    Biquad detector;
    Biquad eq[2];
    int counter = 0;
};

// ---------------------------------------------------------------------------
class Limiter
{
public:
    void prepare (double sampleRate, int maxChannels = 2)
    {
        fs = sampleRate;
        look = std::max (1, (int) std::round (0.005 * fs)); // 5 ms Lookahead
        relCoef = std::exp (-1.0 / (0.080 * fs));

        // Audio wird um look + kTpDelay verzoegert (True-Peak-Interpolation braucht Zukunft)
        delayLen = look + kTpDelay;
        delay.assign ((size_t) std::max (2, maxChannels), std::vector<float> ((size_t) delayLen, 0.0f));
        dPos = 0;

        for (auto& h : hist) h.fill (0.0f);
        histPos = 0;
        buildInterpolator();

        reqRing.assign ((size_t) look + 1, 1.0);
        dqIdx.assign ((size_t) look + 2, 0);
        dqHead = dqTail = 0;
        sampleIndex = 0;

        boxRing.assign ((size_t) look, 1.0);
        boxPos = 0;
        boxSum = (double) look;
        rel = 1.0;
        lastGainDb = 0.0;
    }

    int getLatencySamples() const { return delayLen; }
    void setCeilingDb (double db) { ceiling = dbToGain (db); }
    double currentGainReductionDb() const { return -lastGainDb; }

    // Verarbeitet ein Stereo-Sample in-place (Ausgang ist um getLatencySamples() verzoegert)
    inline void process (float& l, float& r) noexcept
    {
        // True Peak: Sample-Spitze plus 3 interpolierte Zwischenwerte (4x Oversampling)
        hist[0][(size_t) histPos] = l;
        hist[1][(size_t) histPos] = r;
        histPos = (histPos + 1) % kTaps;
        double peak = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            // Sample zum Zeitpunkt n - kTpDelay
            peak = std::max (peak, (double) std::abs (hist[(size_t) c][(size_t) ((histPos + kTaps - 1 - kTpDelay) % kTaps)]));
            for (int ph = 0; ph < 3; ++ph)
            {
                double y = 0.0;
                for (int k = 0; k < kTaps; ++k) // hist[(histPos + k) % kTaps] = x[n - 11 + k]
                    y += interp[(size_t) ph][(size_t) k] * hist[(size_t) c][(size_t) ((histPos + k) % kTaps)];
                peak = std::max (peak, std::abs (y));
            }
        }
        const double req = peak > ceiling ? ceiling / peak : 1.0;

        // Gleitendes Minimum der letzten look+1 benoetigten Gains (monotone Deque)
        const size_t ringSize = reqRing.size();
        reqRing[(size_t) (sampleIndex % (long long) ringSize)] = req;
        const size_t dqCap = dqIdx.size();
        while (dqHead != dqTail)
        {
            const long long back = dqIdx[(dqTail + dqCap - 1) % dqCap];
            if (reqRing[(size_t) (back % (long long) ringSize)] >= req) dqTail = (dqTail + dqCap - 1) % dqCap;
            else break;
        }
        dqIdx[dqTail] = sampleIndex;
        dqTail = (dqTail + 1) % dqCap;
        while (dqIdx[dqHead] < sampleIndex - look)
            dqHead = (dqHead + 1) % dqCap;
        const double held = reqRing[(size_t) (dqIdx[dqHead] % (long long) ringSize)];
        ++sampleIndex;

        // Release
        rel = held < rel ? held : rel + (held - rel) * (1.0 - relCoef);

        // Boxcar ueber look Samples (glatter Attack, erreicht das Ziel genau am Peak)
        boxSum += rel - boxRing[(size_t) boxPos];
        boxRing[(size_t) boxPos] = rel;
        boxPos = (boxPos + 1) % look;
        if (boxPos == 0) // Drift vermeiden
        {
            boxSum = 0.0;
            for (double v : boxRing) boxSum += v;
        }
        const double g = std::min (1.0, boxSum / look);
        lastGainDb = gainToDb (g);

        // Verzoegerte Samples ausgeben
        const float dl = delay[0][(size_t) dPos], dr = delay[1][(size_t) dPos];
        delay[0][(size_t) dPos] = l;
        delay[1][(size_t) dPos] = r;
        dPos = (dPos + 1) % delayLen; // Ring der Laenge delayLen -> genau delayLen Samples Verzoegerung
        const float lim = (float) ceiling;
        l = std::clamp ((float) (dl * g), -lim, lim);
        r = std::clamp ((float) (dr * g), -lim, lim);
    }

private:
    static constexpr int kTaps = 12;    // Stuetzstellen fuer die Interpolation
    static constexpr int kTpDelay = 6;  // Interpolationspunkte liegen zwischen x[n-6] und x[n-5]

    void buildInterpolator()
    {
        // Gefenstertes Sinc fuer die Zeitpunkte (n-6) + 1/4, 2/4, 3/4
        for (int ph = 0; ph < 3; ++ph)
        {
            const double frac = (ph + 1) / 4.0;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                // hist-Index k entspricht x[n - 11 + k]; Zeitabstand zu (n - 6 + frac):
                const double t = (double) (k - 11 + kTpDelay) - frac;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double w = 0.5 + 0.5 * std::cos (kPi * t / 6.5); // Hann ueber +-6.5 Samples
                interp[(size_t) ph][(size_t) k] = sinc * w;
                sum += sinc * w;
            }
            for (int k = 0; k < kTaps; ++k)
                interp[(size_t) ph][(size_t) k] /= sum;
        }
    }

    double fs = 48000.0, relCoef = 0.0, ceiling = 0.891;
    int look = 240, delayLen = 246;
    std::array<std::array<float, kTaps>, 2> hist {};
    int histPos = 0;
    std::array<std::array<double, kTaps>, 3> interp {};
    std::vector<std::vector<float>> delay;
    int dPos = 0;
    std::vector<double> reqRing;
    std::vector<long long> dqIdx;
    size_t dqHead = 0, dqTail = 0;
    long long sampleIndex = 0;
    std::vector<double> boxRing;
    int boxPos = 0;
    double boxSum = 0.0, rel = 1.0, lastGainDb = 0.0;
};

// ---------------------------------------------------------------------------
class StereoShaper
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : hp) b.reset();
        setMonoBelow (monoFreq, monoOn);
    }

    void setMonoBelow (double hz, bool enabled)
    {
        monoFreq = hz;
        monoOn = enabled;
        hp[0].c = makeCoeffs (FilterType::HighPass, hz, 0.54119610, 0.0, fs);
        hp[1].c = makeCoeffs (FilterType::HighPass, hz, 1.30656296, 0.0, fs);
    }

    void setWidth (double w) { width = w; }

    inline void process (float& l, float& r) noexcept
    {
        const float m = 0.5f * (l + r);
        float s = 0.5f * (l - r);
        if (monoOn)
            s = hp[1].process (hp[0].process (s));
        s *= (float) width;
        l = m + s;
        r = m - s;
    }

private:
    double fs = 48000.0, monoFreq = 120.0, width = 1.0;
    bool monoOn = true;
    Biquad hp[2];
};

} // namespace ironcurve
