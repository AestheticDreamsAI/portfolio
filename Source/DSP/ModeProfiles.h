// Ironcurve Auto EQ - Zielkurven fuer Modern / Alternative Metal
//
// Die Kurven beschreiben die *relative* Energieverteilung pro Frequenzband
// in einer "pink-flachen" Darstellung (Rosa Rauschen = gerade Linie).
// Absolute Pegel spielen keine Rolle: der Fitter richtet die Kurve vor dem
// Vergleich automatisch auf das gemessene Signal aus.
//
// Die Werte sind ein Startpunkt, abgeleitet aus typischen Metal-Mixes
// (tighte Low-Mids, kontrollierter Fizz, Kick-Klick, Snare-Body).
// Wer eigene Referenzen hat, passt einfach die Punkte an.
#pragma once

#include <cmath>
#include <utility>
#include <vector>

namespace ironcurve
{

struct ModeProfile
{
    const char* name;
    std::vector<std::pair<double, double>> target; // (Hz, dB), log-interpoliert

    double hpfFreq;      // Low-Cut, den der Lernvorgang setzt
    double lpfFreq;      // High-Cut (0 = keiner)
    double focusLo;      // Bereich, in dem der EQ angepasst wird
    double focusHi;
    double resLo;        // Suchbereich fuer stoerende Resonanzen
    double resHi;
    double lowShelfFreq;
    double highShelfFreq;
    double maxBoostDb;   // Auto-EQ boostet bewusst vorsichtig ...
    double maxCutDb;     // ... und schneidet lieber
};

inline const std::vector<ModeProfile>& modeProfiles()
{
    static const std::vector<ModeProfile> profiles =
    {
        { "Rhythmus-Gitarre",
          { { 60, -18 }, { 90, -8 }, { 130, -2 }, { 200, 0 }, { 300, -1 }, { 450, -2.5 }, { 700, -3 },
            { 1200, -2 }, { 2000, -2 }, { 3000, -3 }, { 4500, -6 }, { 6000, -10 }, { 8000, -16 },
            { 11000, -24 }, { 16000, -32 } },
          90, 11500, 110, 9000, 1800, 7000, 160, 5000, 3.0, -8.0 },

        { "Lead-Gitarre",
          { { 80, -20 }, { 120, -10 }, { 200, -4 }, { 400, -1 }, { 800, 0 }, { 1500, 0 }, { 2500, -1 },
            { 4000, -4 }, { 6000, -9 }, { 8000, -15 }, { 12000, -24 } },
          120, 11000, 140, 8500, 1800, 7000, 220, 5000, 3.0, -8.0 },

        { "Bass",
          { { 30, -10 }, { 40, -3 }, { 60, 0 }, { 100, 0 }, { 200, -3 }, { 400, -6.5 }, { 800, -5.5 },
            { 1200, -4.5 }, { 2000, -6 }, { 3000, -9 }, { 5000, -14 }, { 8000, -21 }, { 12000, -28 } },
          35, 9000, 45, 6000, 180, 600, 90, 2500, 3.0, -8.0 },

        { "Kick",
          { { 30, -8 }, { 45, -2 }, { 60, 0 }, { 80, 0 }, { 120, -3 }, { 200, -8 }, { 350, -13 },
            { 600, -14 }, { 1000, -13 }, { 2000, -10 }, { 3500, -6 }, { 5000, -6 }, { 7000, -9 },
            { 10000, -15 }, { 16000, -24 } },
          30, 16000, 40, 12000, 200, 700, 70, 3500, 4.0, -9.0 },

        { "Snare",
          { { 60, -18 }, { 100, -8 }, { 150, -2 }, { 220, 0 }, { 350, -2.5 }, { 600, -5.5 }, { 1000, -7 },
            { 2000, -7 }, { 4000, -6 }, { 6000, -7 }, { 9000, -10 }, { 13000, -15 }, { 18000, -22 } },
          70, 0, 90, 15000, 400, 1400, 180, 6000, 3.5, -9.0 },

        { "Overheads",
          { { 100, -22 }, { 200, -14 }, { 400, -9 }, { 800, -7 }, { 1500, -6.5 }, { 3000, -5.5 },
            { 5000, -6 }, { 8000, -7 }, { 12000, -9 }, { 16000, -14 } },
          180, 0, 220, 17000, 1500, 6000, 400, 9000, 3.0, -8.0 },

        { "Vocals",
          { { 70, -20 }, { 100, -10 }, { 150, -4 }, { 250, -2 }, { 400, -3 }, { 800, -2 }, { 1500, -1 },
            { 3000, -1 }, { 5000, -4 }, { 8000, -8 }, { 12000, -13 }, { 16000, -20 } },
          90, 17000, 110, 13000, 2000, 7500, 200, 7000, 3.0, -8.0 },

        { "Mix-Bus",
          { { 25, -10 }, { 35, -3 }, { 50, 0 }, { 80, 0 }, { 120, -1 }, { 250, -3 }, { 500, -5 },
            { 1000, -6 }, { 2000, -6.5 }, { 4000, -8 }, { 8000, -11 }, { 12000, -15 }, { 16000, -20 } },
          25, 0, 30, 16000, 2000, 6000, 100, 8000, 2.0, -4.0 },

        // ---- Ab hier: Layer-Instrumente. Neue Modi immer hinten anhaengen,
        //      damit gespeicherte FL-Projekte ihren Modus behalten. ----------

        // Pads/Synths machen Platz: Senke bei 1-3 kHz (Gitarren-Biss), Luft oben
        { "Pads / Synths",
          { { 80, -16 }, { 150, -6 }, { 250, -3 }, { 400, -4 }, { 800, -6 }, { 1500, -8.5 }, { 2500, -9 },
            { 4000, -8 }, { 6000, -7.5 }, { 9000, -8.5 }, { 12000, -10.5 }, { 16000, -15 } },
          150, 0, 180, 16000, 300, 5000, 250, 9000, 3.0, -8.0 },

        // Streicher: Koerper 200-400 Hz, Kratzen bei 2-4 kHz zaehmen, Gitarren-Mitten frei
        { "Strings / Orchester",
          { { 60, -20 }, { 100, -10 }, { 200, -3 }, { 350, -2 }, { 600, -4 }, { 1000, -5.5 }, { 1800, -7 },
            { 3000, -8 }, { 5000, -9 }, { 8000, -11 }, { 12000, -15 } },
          90, 16000, 110, 13000, 2000, 5000, 180, 7000, 3.0, -8.0 },

        { "Clean-Gitarre",
          { { 70, -18 }, { 100, -8 }, { 150, -3 }, { 250, -2 }, { 400, -3 }, { 800, -3 }, { 1500, -2.5 },
            { 3000, -2.5 }, { 5000, -4 }, { 8000, -7 }, { 12000, -12 } },
          90, 16000, 110, 13000, 1500, 6000, 160, 6000, 3.0, -8.0 },

        // Delay-/Reverb-Leads im Hintergrund: schlank unten, gezaehmte Hoehen
        { "Ambient-Gitarre",
          { { 120, -20 }, { 200, -9 }, { 400, -5 }, { 800, -5 }, { 1500, -6 }, { 2500, -6 }, { 4000, -7 },
            { 6000, -9 }, { 9000, -12 }, { 12000, -16 } },
          200, 12000, 240, 10000, 1500, 6000, 300, 6000, 2.5, -9.0 },

        { "Piano / Keys",
          { { 50, -14 }, { 80, -6 }, { 150, -2 }, { 300, -2 }, { 500, -4 }, { 1000, -5 }, { 2000, -6 },
            { 4000, -7.5 }, { 7000, -10 }, { 12000, -15 } },
          100, 0, 120, 15000, 200, 800, 200, 8000, 3.0, -8.0 },

        // Toms: Punch unten, Ringen/Pappe 250-900 Hz raus, etwas Attack
        { "Toms",
          { { 50, -8 }, { 80, -1 }, { 120, 0 }, { 200, -3 }, { 400, -10 }, { 700, -13 }, { 1200, -12 },
            { 3000, -8 }, { 5000, -8 }, { 8000, -13 }, { 12000, -20 } },
          50, 14000, 60, 10000, 250, 900, 100, 5000, 3.5, -9.0 },

        // 808 / Sub-Drops: bewusst nur Tiefbass
        { "808 / Sub-Drop",
          { { 25, -6 }, { 35, 0 }, { 50, 0 }, { 70, -1 }, { 100, -4 }, { 200, -12 }, { 400, -20 },
            { 800, -26 }, { 2000, -32 }, { 5000, -40 } },
          25, 4000, 30, 3000, 80, 400, 60, 1000, 3.0, -10.0 },

        { "Gang-Vocals / Chor",
          { { 100, -20 }, { 150, -9 }, { 250, -3 }, { 400, -3 }, { 800, -2.5 }, { 1500, -2 }, { 3000, -3 },
            { 5000, -6 }, { 8000, -9 }, { 12000, -13 }, { 16000, -19 } },
          140, 15000, 160, 12000, 2000, 7000, 250, 7000, 3.0, -8.0 },

        // Risers, Impacts, Glitches: neutral und vorsichtig
        { "Samples / FX",
          { { 30, -6 }, { 50, -2 }, { 100, -1 }, { 250, -2 }, { 500, -3.5 }, { 1000, -4.5 }, { 2000, -5.5 },
            { 4000, -6.5 }, { 8000, -8.5 }, { 12000, -11 }, { 16000, -15 } },
          30, 0, 40, 16000, 1500, 8000, 120, 8000, 2.5, -6.0 },
    };

    return profiles;
}

// Logarithmisch interpolierter Zielwert
inline double targetDbAt (const ModeProfile& p, double f)
{
    const auto& t = p.target;
    if (f <= t.front().first) return t.front().second;
    if (f >= t.back().first)  return t.back().second;

    for (size_t i = 1; i < t.size(); ++i)
    {
        if (f <= t[i].first)
        {
            const double x0 = std::log2 (t[i - 1].first), x1 = std::log2 (t[i].first);
            const double a = (std::log2 (f) - x0) / (x1 - x0);
            return t[i - 1].second + a * (t[i].second - t[i - 1].second);
        }
    }
    return t.back().second;
}

} // namespace ironcurve
