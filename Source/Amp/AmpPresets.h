// Ironcurve Amp - feste Klang-Presets (keine Regler noetig)
#pragma once

#include "../DSP/AmpDSP.h"
#include <array>

namespace ironcurve
{
struct AmpPreset
{
    const char* name;   // UTF-8
    const char* hint;   // kurze Beschreibung in Alltagssprache
    bool bass;
    AmpParams p;
};

inline AmpParams mk (double gate, double tight, double gain, double b, double m, double t, double pres, double depth,
                     double levelDb, double xover = 200.0, double lowDb = 0.0, double grit = 0.6)
{
    AmpParams p;
    p.gate = gate; p.tight = tight; p.gain = gain;
    p.bass = b; p.mid = m; p.treble = t; p.presence = pres; p.depth = depth;
    p.levelDb = levelDb; p.cab = true;
    p.crossoverHz = xover; p.lowDb = lowDb; p.grit = grit;
    return p;
}

// Lautstaerke (levelDb) ist so abgeglichen, dass alle Presets gleich laut klingen.
// Die Band-Namen beschreiben nur die Klangrichtung; es sind keine Kopien.
constexpr int kNumAmpPresets = 12;
inline const std::array<AmpPreset, kNumAmpPresets>& ampPresets()
{
    static const std::array<AmpPreset, kNumAmpPresets> list { {
        //                                                                                   gate tight gain  bass  mid   treb  pres  depth level
        { "Dunkel & Breit",     "Richtung Bad Omens: dunkel, groß, poliert",             false, mk (0.50, 0.60, 0.65, 0.58, 0.45, 0.42, 0.42, 0.60, -3.2) },
        { "Brutal Tight",       "Richtung Architects: hart, präzise, aggressiv",         false, mk (0.60, 0.80, 0.72, 0.42, 0.55, 0.55, 0.60, 0.42, -1.9) },
        { "Metalcore Klassik",  "Richtung frühe BMTH / Killswitch: roh und bissig",      false, mk (0.55, 0.70, 0.75, 0.52, 0.55, 0.55, 0.62, 0.50, -2.7) },
        { "Riesig & Weit",      "Richtung Sleep Token: weniger Gain, viel Wucht",        false, mk (0.40, 0.55, 0.55, 0.60, 0.45, 0.48, 0.42, 0.65, -3.6) },
        { "Djent",              "Richtung Spiritbox / Periphery: knackig, klar",         false, mk (0.60, 0.85, 0.62, 0.40, 0.60, 0.58, 0.62, 0.38, -1.8) },
        { "Alternative Crunch", "Richtung Deftones: rauer, weniger verzerrt",            false, mk (0.30, 0.55, 0.45, 0.52, 0.58, 0.50, 0.50, 0.50, -2.8) },
        { "Modern Metal",       "Allrounder: fett, tight, druckvoll",                    false, mk (0.50, 0.65, 0.65, 0.50, 0.50, 0.50, 0.50, 0.50, -2.5) },
        { "Solo",               "Für Leads: singend, setzt sich durch",                  false, mk (0.35, 0.60, 0.75, 0.45, 0.65, 0.52, 0.55, 0.45, -2.4) },
        { "Metalcore Bass",     "Richtung Bad Omens: sauberes Fundament + Knurren",      true,  mk (0.50, 0.60, 0.62, 0.52, 0.48, 0.48, 0.50, 0.50, -4.8, 200.0, 0.0, 0.55) },
        { "Knurrig",            "Viel Biss, für harte Parts",                            true,  mk (0.50, 0.60, 0.80, 0.50, 0.65, 0.55, 0.50, 0.50, -10.1, 250.0, -1.0, 0.80) },
        { "Klick",              "Richtung Djent: klarer Anschlag, schneidet durch",     true,  mk (0.55, 0.60, 0.70, 0.45, 0.55, 0.70, 0.50, 0.50, -8.1, 220.0, 0.0, 0.65) },
        { "Tief & Sauber",      "Fast unverzerrt, nur Druck",                            true,  mk (0.45, 0.60, 0.35, 0.55, 0.45, 0.45, 0.50, 0.50, 1.0, 150.0, 2.0, 0.30) },
    } };
    return list;
}
} // namespace ironcurve
