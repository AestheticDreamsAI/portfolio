// Ironcurve Auto EQ - Biquad-Filter (RBJ Audio EQ Cookbook)
// Reines C++ ohne JUCE, damit der Kern unabhaengig testbar ist.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace ironcurve
{

constexpr double kPi = 3.14159265358979323846;

enum class FilterType
{
    HighPass,   // 2. Ordnung, Q steuert die Kante
    LowPass,
    LowShelf,
    HighShelf,
    Peak
};

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // Betragsfrequenzgang in dB bei Frequenz f
    double magnitudeDb (double f, double fs) const
    {
        const double w = 2.0 * kPi * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        const double mag = std::abs (num / den);
        return 20.0 * std::log10 (std::max (mag, 1.0e-12));
    }
};

inline BiquadCoeffs makeCoeffs (FilterType type, double freq, double q, double gainDb, double fs)
{
    freq = std::clamp (freq, 10.0, fs * 0.49);
    q = std::max (q, 0.05);

    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / fs;
    const double cw = std::cos (w0);
    const double sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case FilterType::HighPass:
            b0 = (1.0 + cw) * 0.5;  b1 = -(1.0 + cw);  b2 = (1.0 + cw) * 0.5;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;

        case FilterType::LowPass:
            b0 = (1.0 - cw) * 0.5;  b1 = 1.0 - cw;     b2 = (1.0 - cw) * 0.5;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;

        case FilterType::Peak:
            b0 = 1.0 + alpha * A;   b1 = -2.0 * cw;    b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;   a1 = -2.0 * cw;    a2 = 1.0 - alpha / A;
            break;

        case FilterType::LowShelf:
        {
            const double sq = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + sq);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sq);
            a0 = (A + 1) + (A - 1) * cw + sq;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sq;
            break;
        }

        case FilterType::HighShelf:
        {
            const double sq = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + sq);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sq);
            a0 = (A + 1) - (A - 1) * cw + sq;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sq;
            break;
        }
    }

    BiquadCoeffs c;
    c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
    c.a1 = a1 / a0;  c.a2 = a2 / a0;
    return c;
}

// Transposed Direct Form II, numerisch stabil bei Automation
struct Biquad
{
    BiquadCoeffs c;
    double s1 = 0.0, s2 = 0.0;

    void reset() noexcept { s1 = s2 = 0.0; }

    inline float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return static_cast<float> (y);
    }
};

} // namespace ironcurve
