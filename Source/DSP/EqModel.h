// Ironcurve Auto EQ - Bandlayout und Gesamtfrequenzgang
#pragma once

#include "Biquad.h"
#include <array>

namespace ironcurve
{

// Feste Bandstruktur: so bleibt jeder gelernte Wert ein normaler,
// automatisierbarer Parameter in FL Studio.
enum BandIndex
{
    kHpf = 0,
    kLowShelf,
    kBell1, kBell2, kBell3, kBell4,   // breite Klangbaender
    kRes1, kRes2,                     // schmale Resonanz-Notches
    kHighShelf,
    kLpf,
    kNumBands
};

// Butterworth 24 dB/Okt. = zwei kaskadierte Biquads
constexpr double kButterQ1 = 0.54119610;
constexpr double kButterQ2 = 1.30656296;

inline FilterType bandType (int band)
{
    switch (band)
    {
        case kHpf:       return FilterType::HighPass;
        case kLpf:       return FilterType::LowPass;
        case kLowShelf:  return FilterType::LowShelf;
        case kHighShelf: return FilterType::HighShelf;
        default:         return FilterType::Peak;
    }
}

inline bool bandHasGain (int band) { return band != kHpf && band != kLpf; }
inline bool bandHasQ    (int band) { return band != kHpf && band != kLpf; }

struct BandSettings
{
    bool enabled = false;
    double freq = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
};

struct EqSettings
{
    std::array<BandSettings, kNumBands> bands {};

    static EqSettings flat()
    {
        EqSettings s;
        const double defaults[kNumBands] = { 80, 120, 250, 600, 1500, 3500, 2500, 5000, 8000, 14000 };
        for (int i = 0; i < kNumBands; ++i)
        {
            s.bands[(size_t) i].enabled = false;
            s.bands[(size_t) i].freq = defaults[i];
            s.bands[(size_t) i].gainDb = 0.0;
            s.bands[(size_t) i].q = (i == kRes1 || i == kRes2) ? 6.0 : (i == kLowShelf || i == kHighShelf ? 0.7 : 1.0);
        }
        return s;
    }
};

// Frequenzgang eines einzelnen Bandes (dB). amount skaliert Gain-Baender.
inline double bandResponseDb (const BandSettings& b, int band, double amount, double f, double fs)
{
    if (! b.enabled)
        return 0.0;

    const auto type = bandType (band);

    if (type == FilterType::HighPass || type == FilterType::LowPass)
        return makeCoeffs (type, b.freq, kButterQ1, 0.0, fs).magnitudeDb (f, fs)
             + makeCoeffs (type, b.freq, kButterQ2, 0.0, fs).magnitudeDb (f, fs);

    const double g = b.gainDb * amount;
    if (std::abs (g) < 1.0e-6)
        return 0.0;

    return makeCoeffs (type, b.freq, b.q, g, fs).magnitudeDb (f, fs);
}

inline double eqResponseDb (const EqSettings& eq, double amount, double f, double fs)
{
    double sum = 0.0;
    for (int i = 0; i < kNumBands; ++i)
        sum += bandResponseDb (eq.bands[(size_t) i], i, amount, f, fs);
    return sum;
}

} // namespace ironcurve
