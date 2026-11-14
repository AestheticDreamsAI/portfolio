// Ironcurve Auto EQ - berechnet aus einem gelernten Spektrum die EQ-Baender
#pragma once

#include "EqModel.h"
#include "ModeProfiles.h"
#include <vector>

namespace ironcurve
{

// Analyseraster: 1/24 Oktave von 20 Hz bis 20 kHz
struct AnalysisGrid
{
    static constexpr double kFirstHz = 20.0;
    static constexpr int kBandsPerOctave = 24;
    static constexpr int kNumBands = 240; // 10 Oktaven

    static double centerHz (int i)  { return kFirstHz * std::pow (2.0, (i + 0.5) / kBandsPerOctave); }
    static double lowerHz (int i)   { return kFirstHz * std::pow (2.0, (double) i / kBandsPerOctave); }
    static double upperHz (int i)   { return kFirstHz * std::pow (2.0, (double) (i + 1) / kBandsPerOctave); }
};

struct FitResult
{
    EqSettings eq;
    double outputTrimDb = 0.0;          // grobe Lautheitskompensation
    std::vector<double> alignedTargetDb; // Zielkurve, auf das Signal ausgerichtet (fuer die Anzeige)
};

// measuredDb: Leistung pro Analyseband in dB (pink-flach, Laenge AnalysisGrid::kNumBands)
FitResult fitEq (const ModeProfile& profile, const std::vector<double>& measuredDb, double sampleRate);

// Zielkurve aus einem Referenz-Spektrum (z. B. eigene fruehere Version) bauen
ModeProfile makeReferenceProfile (const std::vector<double>& referenceDb);

// Grobe Lautheit (K-gewichtet) aus Bandleistungen; optional mit EQ davor.
// Fuer Vergleiche zwischen zwei Spektren gedacht (gleiche Methode auf beiden Seiten).
double approxLoudnessDb (const std::vector<double>& bandDb, const EqSettings* eq, double amount, double sampleRate);

// Hilfsfunktion: gleitender Mittelwert ueber +-halfWidth Baender (in dB)
std::vector<double> smoothDb (const std::vector<double>& in, int halfWidth);

} // namespace ironcurve
