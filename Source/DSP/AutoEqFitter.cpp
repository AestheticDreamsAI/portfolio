// Ironcurve Auto EQ - Fitting-Algorithmus
//
// Ablauf:
//  1. Gemessenes Spektrum glaetten (1/3 Okt.) und Zielkurve des Modus darauf ausrichten
//  2. Low-/High-Cut nach Modus setzen
//  3. Schmale Resonanzen suchen (Peaks deutlich ueber der 2/3-Okt.-Glaettung) -> Notches
//  4. Shelves fuer die grobe Tonalitaet unten/oben
//  5. Vier breite Glocken gierig auf die groessten Restabweichungen setzen
//  6. Gains per Least-Squares nachjustieren, unwirksame Baender abschalten
//  7. Ausgangspegel grob (A-gewichtet) kompensieren
#include "AutoEqFitter.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ironcurve
{

std::vector<double> smoothDb (const std::vector<double>& in, int halfWidth)
{
    const int n = (int) in.size();
    std::vector<double> out ((size_t) n, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - halfWidth);
        const int b = std::min (n - 1, i + halfWidth);
        double sum = 0.0;
        for (int j = a; j <= b; ++j)
            sum += in[(size_t) j];
        out[(size_t) i] = sum / (double) (b - a + 1);
    }
    return out;
}

namespace
{
    double qFromBandwidth (double octaves)
    {
        const double p = std::pow (2.0, octaves);
        return std::sqrt (p) / (p - 1.0);
    }

    double aWeightingDb (double f)
    {
        const double f2 = f * f;
        const double num = 12194.0 * 12194.0 * f2 * f2;
        const double den = (f2 + 20.6 * 20.6)
                         * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
                         * (f2 + 12194.0 * 12194.0);
        return 20.0 * std::log10 (std::max (num / den, 1.0e-12)) + 2.0;
    }

    struct Fitter
    {
        const ModeProfile& profile;
        double fs;
        int n = AnalysisGrid::kNumBands;
        std::vector<double> freqs;
        std::vector<bool> focus;
        std::vector<double> residual; // gewuenschte Korrektur, die noch fehlt (dB)

        Fitter (const ModeProfile& p, double sampleRate) : profile (p), fs (sampleRate)
        {
            freqs.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
                freqs[(size_t) i] = AnalysisGrid::centerHz (i);
        }

        std::vector<double> response (const BandSettings& b, int band) const
        {
            std::vector<double> r ((size_t) n, 0.0);
            for (int i = 0; i < n; ++i)
                if (freqs[(size_t) i] < fs * 0.5)
                    r[(size_t) i] = bandResponseDb (b, band, 1.0, freqs[(size_t) i], fs);
            return r;
        }

        void subtract (const BandSettings& b, int band)
        {
            const auto r = response (b, band);
            for (int i = 0; i < n; ++i)
                if (focus[(size_t) i])
                    residual[(size_t) i] -= r[(size_t) i];
        }

        void add (const BandSettings& b, int band)
        {
            const auto r = response (b, band);
            for (int i = 0; i < n; ++i)
                if (focus[(size_t) i])
                    residual[(size_t) i] += r[(size_t) i];
        }

        double clampGain (double g) const { return std::clamp (g, profile.maxCutDb, profile.maxBoostDb); }
    };
}

FitResult fitEq (const ModeProfile& profile, const std::vector<double>& measuredDb, double sampleRate)
{
    FitResult result;
    result.eq = EqSettings::flat();

    Fitter ft (profile, sampleRate);
    const int n = ft.n;

    if ((int) measuredDb.size() != n)
        return result;

    // --- 1. Glaetten und Zielkurve ausrichten -------------------------------
    const auto measured = smoothDb (measuredDb, 1);  // leichtes Entrauschen
    const auto smooth   = smoothDb (measuredDb, 4);  // ~1/3 Oktave
    const auto wide     = smoothDb (measuredDb, 8);  // ~2/3 Oktave (Resonanz-Referenz)

    const double maxLevel = *std::max_element (measured.begin(), measured.end());

    double lo = std::max (profile.focusLo, profile.hpfFreq * 1.25);
    double hi = std::min (profile.focusHi, sampleRate * 0.45);
    if (profile.lpfFreq > 0.0)
        hi = std::min (hi, profile.lpfFreq / 1.25);

    ft.focus.assign ((size_t) n, false);
    int focusCount = 0;
    for (int i = 0; i < n; ++i)
    {
        const double f = ft.freqs[(size_t) i];
        // Baender ohne nennenswertes Signal duerfen keine Boosts ausloesen
        const bool audible = measured[(size_t) i] > maxLevel - 70.0 && measured[(size_t) i] > -130.0;
        ft.focus[(size_t) i] = f >= lo && f <= hi && audible;
        focusCount += ft.focus[(size_t) i] ? 1 : 0;
    }

    std::vector<double> target ((size_t) n);
    for (int i = 0; i < n; ++i)
        target[(size_t) i] = targetDbAt (profile, ft.freqs[(size_t) i]);

    double offset = 0.0;
    if (focusCount > 0)
    {
        for (int i = 0; i < n; ++i)
            if (ft.focus[(size_t) i])
                offset += smooth[(size_t) i] - target[(size_t) i];
        offset /= focusCount;
    }

    result.alignedTargetDb.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        result.alignedTargetDb[(size_t) i] = target[(size_t) i] + offset;

    ft.residual.assign ((size_t) n, 0.0);
    for (int i = 0; i < n; ++i)
        if (ft.focus[(size_t) i])
            ft.residual[(size_t) i] = result.alignedTargetDb[(size_t) i] - smooth[(size_t) i];

    auto& bands = result.eq.bands;

    // --- 2. Low-/High-Cut ---------------------------------------------------
    bands[kHpf].enabled = true;
    bands[kHpf].freq = profile.hpfFreq;
    ft.subtract (bands[kHpf], kHpf);

    if (profile.lpfFreq > 0.0 && profile.lpfFreq < sampleRate * 0.45)
    {
        bands[kLpf].enabled = true;
        bands[kLpf].freq = profile.lpfFreq;
        ft.subtract (bands[kLpf], kLpf);
    }

    if (focusCount < 8)
        return result; // zu wenig verwertbares Signal: nur Cuts setzen

    // --- 3. Resonanzen ------------------------------------------------------
    {
        std::vector<double> excess ((size_t) n, 0.0);
        for (int i = 0; i < n; ++i)
            excess[(size_t) i] = measured[(size_t) i] - wide[(size_t) i];

        std::vector<int> candidates;
        for (int i = 2; i < n - 2; ++i)
        {
            const double f = ft.freqs[(size_t) i];
            if (! ft.focus[(size_t) i] || f < profile.resLo || f > profile.resHi)
                continue;

            const double e = excess[(size_t) i];
            if (e < 3.0)
                continue;

            bool isMax = true;
            for (int d = -2; d <= 2; ++d)
                if (d != 0 && excess[(size_t) (i + d)] > e)
                    isMax = false;

            if (isMax)
                candidates.push_back (i);
        }

        std::sort (candidates.begin(), candidates.end(),
                   [&] (int a, int b) { return excess[(size_t) a] > excess[(size_t) b]; });

        std::vector<int> chosen;
        for (int c : candidates)
        {
            bool farEnough = true;
            for (int k : chosen)
                if (std::abs (k - c) < 8)
                    farEnough = false;
            if (farEnough)
                chosen.push_back (c);
            if (chosen.size() == 2)
                break;
        }

        const int resBands[2] = { kRes1, kRes2 };
        for (size_t r = 0; r < chosen.size(); ++r)
        {
            const int k = chosen[r];
            const double peak = excess[(size_t) k];

            int left = k, right = k;
            while (left > 0 && excess[(size_t) (left - 1)] > peak * 0.5) --left;
            while (right < n - 1 && excess[(size_t) (right + 1)] > peak * 0.5) ++right;

            const double bwOct = std::max (2, right - left + 1) / (double) AnalysisGrid::kBandsPerOctave;

            // Parabolische Interpolation fuer die genaue Peak-Frequenz
            const double e0 = excess[(size_t) (k - 1)], e1 = excess[(size_t) k], e2 = excess[(size_t) (k + 1)];
            const double denom = e0 - 2.0 * e1 + e2;
            const double delta = std::abs (denom) > 1.0e-9 ? std::clamp (0.5 * (e0 - e2) / denom, -0.5, 0.5) : 0.0;

            auto& b = bands[(size_t) resBands[r]];
            b.enabled = true;
            b.freq = AnalysisGrid::kFirstHz * std::pow (2.0, (k + delta + 0.5) / AnalysisGrid::kBandsPerOctave);
            b.gainDb = -std::min (peak * 0.9, 9.0);
            b.q = std::clamp (qFromBandwidth (bwOct), 2.5, 12.0);
            ft.subtract (b, resBands[r]);
        }
    }

    // --- 4. Shelves ---------------------------------------------------------
    auto fitShelf = [&] (int band, double shelfFreq, bool low)
    {
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < n; ++i)
        {
            const double f = ft.freqs[(size_t) i];
            if (ft.focus[(size_t) i] && (low ? f <= shelfFreq : f >= shelfFreq))
            {
                sum += ft.residual[(size_t) i];
                ++count;
            }
        }

        auto& b = bands[(size_t) band];
        b.freq = shelfFreq;
        b.q = 0.7;

        if (count < 4)
            return;

        const double g = ft.clampGain (0.8 * sum / count);
        if (std::abs (g) < 0.5)
            return;

        b.enabled = true;
        b.gainDb = g;
        ft.subtract (b, band);
    };

    fitShelf (kLowShelf, profile.lowShelfFreq, true);
    fitShelf (kHighShelf, profile.highShelfFreq, false);

    // --- 5. Breite Glocken --------------------------------------------------
    // Sperrzone von +-1/2 Oktave um bereits gesetzte Glocken: sonst landen bei
    // begrenztem Boost mehrere Glocken auf derselben Stelle und addieren sich.
    std::vector<int> placedBells;
    for (int band = kBell1; band <= kBell4; ++band)
    {
        const auto rs = smoothDb (ft.residual, 4);

        int best = -1;
        double bestScore = 0.0;
        for (int i = 0; i < n; ++i)
        {
            if (! ft.focus[(size_t) i])
                continue;
            bool blocked = false;
            for (int p : placedBells)
                blocked |= std::abs (p - i) < AnalysisGrid::kBandsPerOctave / 2;
            if (blocked)
                continue;
            const double v = rs[(size_t) i];
            const double score = std::abs (v) * (v < 0.0 ? 1.0 : 0.8); // Cuts bevorzugen
            if (score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }

        if (best < 0 || std::abs (rs[(size_t) best]) < 0.75)
            break;

        const double peak = rs[(size_t) best];
        auto sameSideAndBig = [&] (int i)
        {
            return ft.focus[(size_t) i] && rs[(size_t) i] * peak > 0.0 && std::abs (rs[(size_t) i]) > std::abs (peak) * 0.5;
        };

        int left = best, right = best;
        while (left > 0 && sameSideAndBig (left - 1)) --left;
        while (right < n - 1 && sameSideAndBig (right + 1)) ++right;

        const double bwOct = std::max (8, right - left + 1) / (double) AnalysisGrid::kBandsPerOctave;

        auto& b = bands[(size_t) band];
        b.enabled = true;
        b.freq = ft.freqs[(size_t) best];
        b.gainDb = ft.clampGain (peak);
        b.q = std::clamp (qFromBandwidth (bwOct), 0.4, 4.0);
        ft.subtract (b, band);
        placedBells.push_back (best);
    }

    // --- 6. Least-Squares-Nachjustierung ------------------------------------
    const int tunable[] = { kLowShelf, kBell1, kBell2, kBell3, kBell4, kHighShelf };
    for (int pass = 0; pass < 4; ++pass)
    {
        for (int band : tunable)
        {
            auto& b = bands[(size_t) band];
            if (! b.enabled || std::abs (b.gainDb) < 1.0e-3)
                continue;

            ft.add (b, band);
            const auto shape = ft.response (b, band);
            double num = 0.0, den = 0.0;
            for (int i = 0; i < n; ++i)
            {
                if (! ft.focus[(size_t) i])
                    continue;
                const double s = shape[(size_t) i] / b.gainDb;
                num += s * ft.residual[(size_t) i];
                den += s * s;
            }

            if (den > 1.0e-9)
                b.gainDb = ft.clampGain (num / den);

            if (std::abs (b.gainDb) < 0.5)
            {
                b.enabled = false;
                b.gainDb = 0.0;
            }
            else
            {
                ft.subtract (b, band);
            }
        }
    }

    // --- 6b. Summen-Boost begrenzen (ueberlappende Glocken addieren sich) ---
    for (int iter = 0; iter < 6; ++iter)
    {
        double maxBoost = 0.0;
        for (int i = 0; i < n; ++i)
            if (ft.focus[(size_t) i])
            {
                double sum = 0.0;
                for (int band : tunable)
                    sum += bandResponseDb (bands[(size_t) band], band, 1.0, ft.freqs[(size_t) i], sampleRate);
                maxBoost = std::max (maxBoost, sum);
            }

        const double limit = profile.maxBoostDb + 0.5;
        if (maxBoost <= limit)
            break;

        const double scale = limit / maxBoost;
        for (int band : tunable)
            if (bands[(size_t) band].enabled && bands[(size_t) band].gainDb > 0.0)
                bands[(size_t) band].gainDb *= scale;
    }

    // --- 7. Lautheitskompensation -------------------------------------------
    {
        double p0 = 0.0, p1 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double f = ft.freqs[(size_t) i];
            if (f >= sampleRate * 0.5)
                break;
            const double w = aWeightingDb (f);
            const double eq = eqResponseDb (result.eq, 1.0, f, sampleRate);
            p0 += std::pow (10.0, (measuredDb[(size_t) i] + w) / 10.0);
            p1 += std::pow (10.0, (measuredDb[(size_t) i] + w + eq) / 10.0);
        }
        if (p0 > 0.0 && p1 > 0.0)
            result.outputTrimDb = std::clamp (-10.0 * std::log10 (p1 / p0), -12.0, 12.0);
    }

    return result;
}

} // namespace ironcurve
