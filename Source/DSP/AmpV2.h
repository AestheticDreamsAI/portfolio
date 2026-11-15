// Ironcurve Amp - Gitarren-Verstaerker, zweite Generation
// Aufbau: Boost (strafft) -> 2 Roehrenstufen -> Dioden-Begrenzer -> Nachstufe -> Endstufe.
// Alle Koppelglieder sind einfache RC-Filter (1. Ordnung) wie in einer echten Schaltung.
#pragma once

#include "AmpDSP.h"

namespace ironcurve
{
struct AmpVoice
{
    // Boost vor dem Amp
    double boostHpHz = 180.0;   // Bass-Straffung
    double boostMidHz = 900.0, boostMidDb = 6.0, boostMidQ = 0.6;
    double boostLpHz = 6000.0;
    // Stufe 1 / 2
    double g1Db = 26.0, hp1Hz = 40.0, lp1Hz = 9000.0, bias1 = 0.15;
    double g2Db = 22.0, hp2Hz = 120.0, lp2Hz = 7000.0, bias2 = 0.25;
    // Dioden-Begrenzer
    double clipDb = 12.0, clipKnee = 3.0, clipAsym = 0.0;
    double hp3Hz = 60.0, lp3Hz = 6000.0;
    // Endstufe
    double powerDb = 6.0, powerBias = 0.05, powerKnee = 2.0;
    double outDb = 0.0;
    // Gitterstrom: Koppelkondensator laedt sich bei Uebersteuerung auf (dynamischer Arbeitspunkt, "Growl")
    double gridAmt = 0.0, gridTauMs = 15.0;
    // Klangformung vor der Verzerrung (4 Baender) und Klangregelung vor der Endstufe (3 Baender), in dB
    double pre[4] = { 0, 0, 0, 0 };
    double tone[3] = { 0, 0, 0 };
    // Netzteil-Nachgeben der Endstufe: laute Anschlaege druecken kurz den Pegel
    double sagAmt = 0.0, sagRelMs = 150.0;
};

class OnePole
{
public:
    void setLp (double hz, double fs) { a = 1.0 - std::exp (-2.0 * kPi * std::min (hz, 0.45 * fs) / fs); }
    inline double lp (double x) noexcept { z += a * (x - z); return z; }
    inline double hp (double x) noexcept { z += a * (x - z); return x - z; }
    void reset() { z = 0.0; }
private:
    double a = 1.0, z = 0.0;
};

// Kennlinie: x/(1+|x|^n)^(1/n). n klein = weich (Roehre), n gross = hart (Dioden)
inline double softClip (double x, double n) noexcept
{
    const double ax = std::abs (x);
    if (ax < 1.0e-9) return x;
    return x / std::pow (1.0 + std::pow (ax, n), 1.0 / n);
}

// Roehrenstufe: oben hart (Gitterstrom), unten weich (Sperrung), mit Arbeitspunkt
inline double tube (double x, double bias) noexcept
{
    const double v = x + bias;
    const double y = v > 0.0 ? softClip (v, 2.5) : 1.25 * std::tanh (v / 1.25);
    const double y0 = bias > 0.0 ? softClip (bias, 2.5) : 1.25 * std::tanh (bias / 1.25);
    return y - y0;
}

struct BqD
{
    BiquadCoeffs c; double s1 = 0.0, s2 = 0.0;
    void reset() { s1 = s2 = 0.0; }
    inline double process (double x) noexcept
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

class GuitarAmpV2
{
public:
    void prepare (double fs)
    {
        sr = fs; fsHigh = fs * Oversampler4x::kFactor;
        os.reset();
        for (auto* f : { &hp1, &lp1, &hp2, &lp2, &hp3, &lp3, &bHp1, &bHp2, &bLp, &dc }) f->reset();
        bMid.reset(); q2 = q3 = 0.0; sagEnv = 0.0;
        for (auto& b : preEq) b.reset();
        for (auto& b : toneEq) b.reset();
        apply();
    }

    void setVoice (const AmpVoice& nv) { v = nv; apply(); }

    inline double process (double x) noexcept
    {
        // Boost bei Basisrate
        double s = bHp2.hp (bHp1.hp (x));
        s = bMid.process ((float) s);
        s = bLp.lp (s);
        for (auto& b : preEq) s = b.process (s);

        double up[Oversampler4x::kFactor];
        os.up (s, up);
        for (auto& u : up)
        {
            double t = tube (hp1.hp (u) * g1, v.bias1);
            t = lp1.lp (t);
            {
                double in2 = hp2.hp (t) * g2 - q2;
                if (in2 > 0.6) q2 += gridK * (in2 - 0.6);
                q2 *= gridDecay;
                t = tube (in2, v.bias2);
            }
            t = lp2.lp (t);
            {
                double in3 = hp3.hp (t) * gClip - q3;
                if (in3 > 0.6) q3 += gridK * (in3 - 0.6);
                q3 *= gridDecay;
                t = softClip (in3 + v.clipAsym, v.clipKnee) - clipOffset;
            }
            t = lp3.lp (t);
            for (auto& b : toneEq) t = b.process (t);
            {
                const double a = std::abs (t);
                sagEnv += (a > sagEnv ? sagAtt : sagRel) * (a - sagEnv);
                t /= (1.0 + v.sagAmt * sagEnv);
            }
            t = softClip (t * gPower + v.powerBias, v.powerKnee) - powerOffset;
            u = t;
        }
        return dc.hp (os.down (up)) * gOut;
    }

private:
    static double db2g (double d) { return std::pow (10.0, d / 20.0); }
    void apply()
    {
        bHp1.setLp (v.boostHpHz, sr); bHp2.setLp (v.boostHpHz * 0.5, sr);
        bMid.c = makeCoeffs (FilterType::Peak, v.boostMidHz, v.boostMidQ, v.boostMidDb, sr);
        bLp.setLp (v.boostLpHz, sr);
        hp1.setLp (v.hp1Hz, fsHigh); lp1.setLp (v.lp1Hz, fsHigh);
        hp2.setLp (v.hp2Hz, fsHigh); lp2.setLp (v.lp2Hz, fsHigh);
        hp3.setLp (v.hp3Hz, fsHigh); lp3.setLp (v.lp3Hz, fsHigh);
        dc.setLp (12.0, sr);
        g1 = db2g (v.g1Db); g2 = db2g (v.g2Db); gClip = db2g (v.clipDb); gPower = db2g (v.powerDb); gOut = db2g (v.outDb);
        static const double pf[4] = { 150.0, 400.0, 1200.0, 3500.0 };
        preEq[0].c = makeCoeffs (FilterType::LowShelf, pf[0], 0.7, v.pre[0], sr);
        preEq[1].c = makeCoeffs (FilterType::Peak, pf[1], 0.7, v.pre[1], sr);
        preEq[2].c = makeCoeffs (FilterType::Peak, pf[2], 0.7, v.pre[2], sr);
        preEq[3].c = makeCoeffs (FilterType::HighShelf, pf[3], 0.7, v.pre[3], sr);
        toneEq[0].c = makeCoeffs (FilterType::LowShelf, 200.0, 0.7, v.tone[0], fsHigh);
        toneEq[1].c = makeCoeffs (FilterType::Peak, 700.0, 0.6, v.tone[1], fsHigh);
        toneEq[2].c = makeCoeffs (FilterType::HighShelf, 2500.0, 0.7, v.tone[2], fsHigh);
        sagAtt = 1.0 - std::exp (-1.0 / (0.003 * fsHigh));
        sagRel = 1.0 - std::exp (-1.0 / (std::max (5.0, v.sagRelMs) * 0.001 * fsHigh));
        gridK = v.gridAmt * 0.02 * (192000.0 / fsHigh);
        gridDecay = std::exp (-1.0 / (std::max (0.5, v.gridTauMs) * 0.001 * fsHigh));
        clipOffset = softClip (v.clipAsym, v.clipKnee);
        powerOffset = softClip (v.powerBias, v.powerKnee);
    }

    AmpVoice v;
    double sr = 48000.0, fsHigh = 192000.0;
    double g1 = 1, g2 = 1, gClip = 1, gPower = 1, gOut = 1, clipOffset = 0, powerOffset = 0, q2 = 0, q3 = 0, gridK = 0, gridDecay = 1, sagEnv = 0, sagAtt = 0, sagRel = 0;
    BqD preEq[4], toneEq[3];
    Oversampler4x os;
    OnePole hp1, lp1, hp2, lp2, hp3, lp3, bHp1, bHp2, bLp, dc;
    Biquad bMid;
};
} // namespace ironcurve

#include "AmpVoiceTuned.h"

namespace ironcurve
{
// Verbindet die Presets (AmpParams, 0..1) mit dem abgestimmten Grundklang.
// Alle Werte auf "Mitte" (0,5 bzw. Gain 0,65 / Tight 0,65) ergeben genau den abgestimmten Klang.
class GuitarAmpChannel2
{
public:
    void prepare (double fs)
    {
        sr = fs; amp.prepare (fs);
        for (auto& b : post) b.reset();
        last = AmpParams(); last.gain = -1.0; // erzwingt Neuberechnung
    }

    void setParams (const AmpParams& p)
    {
        if (p.gain == last.gain && p.tight == last.tight && p.bass == last.bass && p.mid == last.mid && p.treble == last.treble
            && p.presence == last.presence && p.depth == last.depth && p.levelDb == last.levelDb)
            return;
        last = p;
        AmpVoice v = tunedVoice();
        v.g1Db += (p.gain - 0.65) * 24.0;
        const double t = std::pow (2.0, (p.tight - 0.65) * 3.0);
        v.boostHpHz *= t; v.hp2Hz *= std::sqrt (t);
        amp.setVoice (v);
        auto db = [] (double x, double range) { return (x - 0.5) * 2.0 * range; };
        post[0].c = makeCoeffs (FilterType::LowShelf, 140.0, 0.7, db (p.bass, 6.0), sr);
        post[1].c = makeCoeffs (FilterType::Peak, 750.0, 0.7, db (p.mid, 6.0), sr);
        post[2].c = makeCoeffs (FilterType::HighShelf, 3000.0, 0.7, db (p.treble, 6.0), sr);
        post[3].c = makeCoeffs (FilterType::Peak, 4200.0, 1.0, db (p.presence, 5.0), sr);
        post[4].c = makeCoeffs (FilterType::Peak, 95.0, 1.2, db (p.depth, 5.0), sr);
        outGain = std::pow (10.0, p.levelDb / 20.0);
    }

    inline double process (double x) noexcept
    {
        double y = amp.process (x);
        for (auto& b : post) y = b.process (y);
        return y * outGain;
    }

private:
    GuitarAmpV2 amp;
    BqD post[5];
    AmpParams last;
    double sr = 48000.0, outGain = 1.0;
};
} // namespace ironcurve
