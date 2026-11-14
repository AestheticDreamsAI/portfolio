// Ironcurve Master - Klarheits-Stufe (psychoakustischer dynamischer EQ)
//
// Grundlage (veroeffentlichte Hoerforschung, keine fremde Implementierung):
//  * Bark-Skala (Zwicker & Terhardt 1980) als Frequenzaufloesung des Gehoers, 0,5-Bark-Baender
//  * Spreizfunktion der Maskierung (Schroeder, Atal & Hall 1979)
//  * Tonalitaetsabhaengiger Maskierungsabstand ueber die spektrale Flachheit (Johnston 1988),
//    lokal pro Band bestimmt (wie die Partitionen im MPEG-Psychoakustikmodell)
//  * Analyse mit Blackman-Harris-Fenster (kaum Uebersprechen), Bearbeitung mit sqrt-Hann (perfekte Rekonstruktion)
//  * Ruhehoerschwelle (Terhardt 1979)
//  * Lautheits-Erhaltung K-gewichtet nach ITU-R BS.1770 (gleiche Messung wie der Limiter dahinter)
//  * Zeitliche Integration der Bandpegel (~60 ms), aehnlich der Lautheitsintegration des Gehoers
//
// Ablauf pro STFT-Frame (2048 Punkte, 75 % Overlap, ~90 Frames/s bei 48 kHz):
//  1. Leistung pro Bark-Band (beide Kanaele, gekoppelt -> Stereobild bleibt)
//  2. Mithoerschwelle jedes Bandes durch alle anderen Baender (+ Ruhehoerschwelle)
//  3. Recover: Baender mit Inhalt, die teilweise verdeckt sind, werden angehoben
//     Tame:    Baender, die deutlich ueber ihrer spektralen Nachbarschaft liegen, werden abgesenkt
//  4. Glaettung ueber Zeit und Frequenz, Lautheits-Ausgleich, Anwendung als Nullphasen-Gain
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include "Dynamics.h"

namespace ironcurve
{

class Clarity
{
public:
    static constexpr int kFftSize = 2048;
    static constexpr int kHop = kFftSize / 4;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buildFft();
        buildWindow();
        buildBands();

        inFifo.assign ((size_t) kFftSize * 2, 0.0f);   // L und R verschachtelt
        outFifo.assign ((size_t) kFftSize * 2, 0.0f);
        accum.assign ((size_t) kFftSize * 2 * 2, 0.0f);
        rover = kFftSize - kHop;

        bandGainDb.assign ((size_t) numBands, 0.0);
        loudComp = 0.0;
        lastRecover = lastTame = 0.0;
        const double frameRate = fs / kHop;
        gainCoef = std::exp (-1.0 / (0.045 * frameRate));  // ~45 ms
        compCoef = std::exp (-1.0 / (0.400 * frameRate));  // ~400 ms (kein Pumpen)
        powCoef = std::exp (-1.0 / (0.060 * frameRate));   // ~60 ms zeitliche Integration
    }

    // amount 0..1 (0 = komplett neutral, Signal bleibt bit-nah identisch)
    void setAmount (double a) { amount = std::clamp (a, 0.0, 1.0); }

    int getLatencySamples() const { return kFftSize; } // per Impuls nachgemessen

    // Mittlere Anhebung/Absenkung des letzten Frames (fuer Meter)
    double getRecoverDb() const { return lastRecover; }
    double getTameDb() const { return lastTame; }

    inline void process (float& l, float& r) noexcept
    {
        const int latency = kFftSize - kHop;
        inFifo[(size_t) (rover * 2)] = l;
        inFifo[(size_t) (rover * 2 + 1)] = r;
        l = outFifo[(size_t) ((rover - latency) * 2)];
        r = outFifo[(size_t) ((rover - latency) * 2 + 1)];

        if (++rover >= kFftSize)
        {
            rover = latency;
            processFrame();
        }
    }

private:
    // ------------------------------------------------------------------ FFT
    void buildFft()
    {
        twiddle.resize (kFftSize / 2);
        for (int k = 0; k < kFftSize / 2; ++k)
            twiddle[(size_t) k] = std::polar (1.0, -2.0 * kPiC * k / kFftSize);
        bitrev.resize (kFftSize);
        int bits = 0;
        while ((1 << bits) < kFftSize) ++bits;
        for (int i = 0; i < kFftSize; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }
        buf.resize (kFftSize);
        abuf.resize (kFftSize);
    }

    void fft (bool inverse) { fftOn (buf, inverse); }

    void fftOn (std::vector<std::complex<double>>& buf, bool inverse) const
    {
        for (int i = 0; i < kFftSize; ++i)
            if (i < bitrev[(size_t) i])
                std::swap (buf[(size_t) i], buf[(size_t) bitrev[(size_t) i]]);
        for (int len = 2; len <= kFftSize; len <<= 1)
        {
            const int step = kFftSize / len;
            for (int i = 0; i < kFftSize; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    auto w = twiddle[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const auto u = buf[(size_t) (i + j)];
                    const auto v = buf[(size_t) (i + j + len / 2)] * w;
                    buf[(size_t) (i + j)] = u + v;
                    buf[(size_t) (i + j + len / 2)] = u - v;
                }
        }
        if (inverse)
            for (auto& x : buf) x /= (double) kFftSize;
    }

    void buildWindow()
    {
        // sqrt-Hann (periodisch) fuer Analyse und Synthese: Summe der Quadrate bei 75 % Overlap = 2
        window.resize (kFftSize);
        double sumW = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            window[(size_t) i] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPiC * i / kFftSize));
            sumW += window[(size_t) i];
        }
        // Analysefenster: Blackman-Harris (4 Terme, -92 dB Nebenkeulen) gegen spektrales Uebersprechen
        awindow.resize (kFftSize);
        double sumA = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            const double x = 2.0 * kPiC * i / kFftSize;
            awindow[(size_t) i] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
            sumA += awindow[(size_t) i];
        }
        powerNorm = 4.0 / (sumA * sumA); // Sinus mit Amplitude 1 -> ca. 0 dBFS
        (void) sumW;
    }

    // ---------------------------------------------------------- Psychoakustik
    static double hzToBark (double f)
    {
        return 13.0 * std::atan (0.00076 * f) + 3.5 * std::atan ((f / 7500.0) * (f / 7500.0));
    }

    // Ruhehoerschwelle in dB SPL (Terhardt), f in Hz
    static double athDb (double f)
    {
        const double k = std::max (f, 20.0) / 1000.0;
        return 3.64 * std::pow (k, -0.8) - 6.5 * std::exp (-0.6 * (k - 3.3) * (k - 3.3)) + 1.0e-3 * k * k * k * k;
    }

    // Spreizfunktion (Schroeder) in dB, dz = z(Maskierter) - z(Maskierer) in Bark
    static double spreadDb (double dz)
    {
        const double x = dz + 0.474;
        return 15.81 + 7.5 * x - 17.5 * std::sqrt (1.0 + x * x);
    }

    void buildBands()
    {
        // 0,5-Bark-Baender von 20 Hz bis min(Nyquist, 20 kHz)
        const double zMin = hzToBark (20.0), zMax = hzToBark (std::min (fs * 0.5, 20000.0));
        numBands = std::max (8, (int) std::floor ((zMax - zMin) / 0.5));
        bandOfBin.assign ((size_t) kFftSize / 2 + 1, -1);
        binBark.assign ((size_t) kFftSize / 2 + 1, 0.0);
        bandZ.assign ((size_t) numBands, 0.0);
        bandAth.assign ((size_t) numBands, 0.0);
        bandCount.assign ((size_t) numBands, 0);
        bandKWeight.assign ((size_t) numBands, 1.0);
        bandPresence.assign ((size_t) numBands, 1.0);
        bandCompDb.assign ((size_t) numBands, 0.0);
        bandCompTarget.assign ((size_t) numBands, 0.0);
        bandTotal.assign ((size_t) numBands, 0.0);

        for (int k = 1; k <= kFftSize / 2; ++k)
        {
            const double f = k * fs / kFftSize;
            const double z = hzToBark (f);
            binBark[(size_t) k] = z;
            const int b = (int) std::floor ((z - zMin) / 0.5);
            if (f >= 20.0 && b >= 0 && b < numBands)
            {
                bandOfBin[(size_t) k] = b;
                ++bandCount[(size_t) b];
            }
        }
        for (int b = 0; b < numBands; ++b)
        {
            bandZ[(size_t) b] = zMin + (b + 0.5) * 0.5;
            // Mittenfrequenz zurueckrechnen (numerisch) fuer die Ruhehoerschwelle
            double lo = 20.0, hi = fs * 0.5;
            for (int it = 0; it < 40; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                (hzToBark (mid) < bandZ[(size_t) b] ? lo : hi) = mid;
            }
            bandAth[(size_t) b] = athDb (0.5 * (lo + hi));
            {
                const double z = bandZ[(size_t) b];  // 1 kHz ~ 8,5 Bark, 5 kHz ~ 18,5 Bark
                // Recover-Gewicht: im Bass zurueckhaltend (dort waere "Hervorholen" eher Dröhnen),
                // ab ca. 1 kHz bis in die Hoehen voll (Praesenz UND Luft)
                double w = 1.0;
                if (z < 8.0) w = 0.4 + 0.6 * std::clamp ((z - 3.0) / 5.0, 0.0, 1.0);
                bandPresence[(size_t) b] = w;
            }
            const double fc = 0.5 * (lo + hi);
            KWeighting kw; kw.prepare (fs);
            bandKWeight[(size_t) b] = std::pow (10.0, (kw.shelf.magnitudeDb (fc, fs) + kw.highpass.magnitudeDb (fc, fs)) / 10.0);
        }

        // Spreizmatrix vorab (in Leistung)
        spread.assign ((size_t) (numBands * numBands), 0.0);
        for (int i = 0; i < numBands; ++i)
            for (int j = 0; j < numBands; ++j)
                spread[(size_t) (i * numBands + j)] = std::pow (10.0, spreadDb (bandZ[(size_t) i] - bandZ[(size_t) j]) / 10.0);

        power.assign ((size_t) numBands, 0.0);
        bandLogSum.assign ((size_t) numBands, 0.0);
        bandAlpha.assign ((size_t) numBands, 0.0);
        flatWeight.assign ((size_t) numBands, 0.0);
        binPow.assign ((size_t) kFftSize / 2 + 1, 0.0);
        powerSmooth.assign ((size_t) numBands, 0.0);
        maskWeight.assign ((size_t) numBands, 0.0);
        scratchLvl.assign ((size_t) numBands, 0.0);
        scratchTarget.assign ((size_t) numBands, 0.0);
        scratchRec.assign ((size_t) numBands, 0.0);
        scratchU.assign ((size_t) numBands, 0.0);
        binGain.assign ((size_t) kFftSize / 2 + 1, 1.0);
    }

    // ---------------------------------------------------------- Frame
    void processFrame()
    {
        // L + iR in einen komplexen FFT-Puffer (beide Kanaele in einer FFT)
        for (int i = 0; i < kFftSize; ++i)
            buf[(size_t) i] = { inFifo[(size_t) (i * 2)] * window[(size_t) i], inFifo[(size_t) (i * 2 + 1)] * window[(size_t) i] };
        fft (false);

        if (amount > 0.0)
        {
            for (int i = 0; i < kFftSize; ++i)
                abuf[(size_t) i] = { inFifo[(size_t) (i * 2)] * awindow[(size_t) i], inFifo[(size_t) (i * 2 + 1)] * awindow[(size_t) i] };
            fftOn (abuf, false);
        }

        if (amount > 0.0)
            computeGains();
        else
        {
            std::fill (binGain.begin(), binGain.end(), 1.0);
            std::fill (bandGainDb.begin(), bandGainDb.end(), 0.0);
            std::fill (bandCompDb.begin(), bandCompDb.end(), 0.0);
            std::fill (bandTotal.begin(), bandTotal.end(), 0.0);
            loudComp = 0.0;
            lastRecover = lastTame = 0.0;
        }

        // Reeller, symmetrischer Gain -> beide Kanaele bleiben getrennt
        buf[0] *= binGain[0];
        for (int k = 1; k < kFftSize / 2; ++k)
        {
            buf[(size_t) k] *= binGain[(size_t) k];
            buf[(size_t) (kFftSize - k)] *= binGain[(size_t) k];
        }
        buf[(size_t) (kFftSize / 2)] *= binGain[(size_t) (kFftSize / 2)];
        fft (true);

        // Overlap-Add (Fensterquadrat-Summe = 2 -> * 0,5)
        for (int i = 0; i < kFftSize; ++i)
        {
            const double w = window[(size_t) i] * 0.5;
            accum[(size_t) (i * 2)] += (float) (buf[(size_t) i].real() * w);
            accum[(size_t) (i * 2 + 1)] += (float) (buf[(size_t) i].imag() * w);
        }
        for (int i = 0; i < kHop * 2; ++i)
            outFifo[(size_t) i] = accum[(size_t) i];
        std::move (accum.begin() + kHop * 2, accum.end(), accum.begin());
        std::fill (accum.end() - kHop * 2, accum.end(), 0.0f);
        std::move (inFifo.begin() + kHop * 2, inFifo.end(), inFifo.begin());
    }

    void computeGains()
    {
        // 1. Leistung pro Band (Summe beider Kanaele) aus der trennscharfen Analyse-FFT,
        //    dazu pro Band die Summen fuer die spektrale Flachheit (Tonalitaet)
        std::fill (power.begin(), power.end(), 0.0);
        std::fill (bandLogSum.begin(), bandLogSum.end(), 0.0);
        double linSum = 0.0;
        int nBins = 0;
        const double norm = powerNorm;
        for (int k = 1; k < kFftSize / 2; ++k)
        {
            const auto X = abuf[(size_t) k], Y = std::conj (abuf[(size_t) (kFftSize - k)]);
            const auto L = 0.5 * (X + Y);
            const auto R = std::complex<double> (0.0, -0.5) * (X - Y);
            const double p = (std::norm (L) + std::norm (R)) * norm + 1.0e-20;
            binPow[(size_t) k] = p;
            const int b = bandOfBin[(size_t) k];
            if (b >= 0) { power[(size_t) b] += p; bandLogSum[(size_t) b] += std::log (p); linSum += p; ++nBins; }
        }

        // Zeitliche Integration (~60 ms) wie im Gehoer, damit Entscheidungen nicht von Frame zu Frame springen
        for (int b = 0; b < numBands; ++b)
            powerSmooth[(size_t) b] = powCoef * powerSmooth[(size_t) b] + (1.0 - powCoef) * power[(size_t) b];

        if (nBins == 0 || linSum < 1.0e-14) // praktisch Stille: nichts tun, sanft zurueck auf 0
        {
            for (int b = 0; b < numBands; ++b)
            {
                bandGainDb[(size_t) b] *= gainCoef;
                bandCompDb[(size_t) b] *= compCoef;
                bandTotal[(size_t) b] = bandGainDb[(size_t) b] - bandCompDb[(size_t) b];
            }
            applyBandGains();
            return;
        }

        // Tonalitaet pro Band (nach Johnston, aber lokal ueber +-1 Band wie im MPEG-Modell):
        // SFM = geometrisches / arithmetisches Mittel der Bins. Rauschen ~ -2,5 dB, Ton << -10 dB.
        for (int b = 0; b < numBands; ++b)
        {
            double lg = 0.0, li = 0.0; int n = 0;
            for (int j = std::max (0, b - 1); j <= std::min (numBands - 1, b + 1); ++j)
            {
                lg += bandLogSum[(size_t) j];
                li += power[(size_t) j];
                n += bandCount[(size_t) j];
            }
            const double sfmDb = n > 0 ? 10.0 * std::log10 (std::exp (lg / n) / (li / n + 1.0e-30) + 1.0e-30) : 0.0;
            bandAlpha[(size_t) b] = std::clamp ((sfmDb + 2.5) / -25.0, 0.0, 1.0);
        }

        // Pegel in dB SPL (Annahme: 0 dBFS Sinus ~ 100 dB SPL Abhoerlautstaerke)
        constexpr double kCal = 100.0;
        std::vector<double>& lvl = scratchLvl;
        for (int b = 0; b < numBands; ++b)
            lvl[(size_t) b] = 10.0 * std::log10 (powerSmooth[(size_t) b] + 1.0e-20) + kCal;

        // 2. TAME zuerst: Baender, die deutlich ueber ihrer spektralen Nachbarschaft (+-1,5 Bark) liegen
        //    und gut hoerbar sind, werden abgesenkt. Keine Frequenzglaettung (Resonanzen sind schmal).
        std::vector<double>& tameDb = scratchTarget;
        for (int i = 0; i < numBands; ++i)
        {
            // Echte Resonanz = lokale Spitze: muss ueber BEIDEN Seiten liegen (Kanten breiter Bereiche nicht)
            double left = 0.0, right = 0.0; int nl = 0, nr = 0;
            for (int j = std::max (0, i - 3); j < i; ++j) { left += lvl[(size_t) j]; ++nl; }
            for (int j = i + 1; j <= std::min (numBands - 1, i + 3); ++j) { right += lvl[(size_t) j]; ++nr; }
            const double excess = (nl > 0 && nr > 0) ? lvl[(size_t) i] - std::max (left / nl, right / nr) : 0.0;
            const double audible = lvl[(size_t) i] - bandAth[(size_t) i];
            // In den Praesenz-Mitten (ca. 1-5 kHz, 8-18,5 Bark) nur deutliche Resonanzen zaehmen
            const double tameThr = (bandZ[(size_t) i] >= 8.0 && bandZ[(size_t) i] <= 18.5) ? 6.0 : 4.0;
            tameDb[(size_t) i] = (excess > tameThr && audible > 20.0)
                               ? -std::min (amount * 0.6 * (excess - tameThr), 8.0 * amount) : 0.0;
        }

        // 3. RECOVER danach: Mithoerschwelle aus den bereits GEZAEHMTEN Pegeln (Johnston-Abstand,
        //    mindestens Ruhehoerschwelle). Nur was dann noch verdeckt ist, wird hervorgeholt.
        for (int b = 0; b < numBands; ++b)
        {
            const double alpha = bandAlpha[(size_t) b];
            const double offset = alpha * (14.5 + bandZ[(size_t) b]) + (1.0 - alpha) * 5.5;
            maskWeight[(size_t) b] = powerSmooth[(size_t) b] * std::pow (10.0, (tameDb[(size_t) b] - offset) / 10.0);
            flatWeight[(size_t) b] = std::pow (10.0, -offset / 10.0);
        }
        std::vector<double>& recDb = scratchRec;
        for (int i = 0; i < numBands; ++i)
        {
            double masking = 0.0, flat = 0.0;
            for (int j = 0; j < numBands; ++j)
                if (j != i)
                {
                    masking += maskWeight[(size_t) j] * spread[(size_t) (i * numBands + j)];
                    flat += flatWeight[(size_t) j] * spread[(size_t) (i * numBands + j)];
                }
            const double thrDb = std::max (10.0 * std::log10 (masking + 1.0e-20) + kCal, bandAth[(size_t) i]);
            const double own = lvl[(size_t) i] + tameDb[(size_t) i];
            const double smr = own - thrDb;
            // Verdeckung, die ein Band in einem gleichmaessigen Spektrum ganz normal haette:
            const double smrFlat = -10.0 * std::log10 (flat + 1.0e-20);
            const double extraMasked = smrFlat - smr;   // > 0: staerker verdeckt als normal
            const double audible = lvl[(size_t) i] - bandAth[(size_t) i];
            recDb[(size_t) i] = (audible > 15.0 && tameDb[(size_t) i] == 0.0 && extraMasked > 0.5)
                              ? std::min (amount * 0.75 * bandPresence[(size_t) i] * std::clamp (extraMasked - 0.5, 0.0, 12.0), 6.0 * amount) : 0.0;
        }

        // 4. Recover ueber Frequenz glaetten (1-2-1), Tame nicht; dann ueber Zeit
        double recSum = 0.0, tameSum = 0.0;
        for (int i = 0; i < numBands; ++i)
        {
            const double a = recDb[(size_t) std::max (0, i - 1)], c = recDb[(size_t) std::min (numBands - 1, i + 1)];
            double rec = 0.25 * a + 0.5 * recDb[(size_t) i] + 0.25 * c;
            if (tameDb[(size_t) i] < 0.0) rec = 0.0; // gezaehmte Baender nie wieder anheben
            const double t = rec + tameDb[(size_t) i];
            bandGainDb[(size_t) i] = gainCoef * bandGainDb[(size_t) i] + (1.0 - gainCoef) * t;
            if (bandGainDb[(size_t) i] > 0) recSum += bandGainDb[(size_t) i]; else tameSum += bandGainDb[(size_t) i];
        }
        lastRecover = recSum / numBands;
        lastTame = tameSum / numBands;

        // 5. Lautheits-Ausgleich nach BS.1770 (K-gewichtete Energie), aber NICHT pauschal:
        //    Die zusaetzliche Energie wird aus den Baendern genommen, die deutlich lauter als der Rest
        //    sind (die Verdecker, im Metal meist Bass-/Gitarrenkoerper). Angehobene Baender und der Rest
        //    bleiben unangetastet. Nur was so nicht reicht, wird global ausgeglichen.
        double eBefore = 0.0, meanLvl = 0.0; int nAud = 0;
        for (int b = 0; b < numBands; ++b)
        {
            eBefore += powerSmooth[(size_t) b] * bandKWeight[(size_t) b];
            if (lvl[(size_t) b] > bandAth[(size_t) b] + 15.0) { meanLvl += lvl[(size_t) b]; ++nAud; }
        }
        meanLvl = nAud > 0 ? meanLvl / nAud : 0.0;

        double uMax = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            const bool candidate = bandGainDb[(size_t) b] <= 0.25 && lvl[(size_t) b] > bandAth[(size_t) b] + 15.0;
            const double u = candidate ? std::max (0.0, lvl[(size_t) b] - meanLvl) : 0.0;
            scratchU[(size_t) b] = u;
            uMax = std::max (uMax, u);
        }

        const double maxCompCut = 4.0 * amount;
        auto energyWith = [&] (double c)
        {
            double e = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                const double cut = uMax > 0.0 ? std::min (c * scratchU[(size_t) b] / uMax, maxCompCut) : 0.0;
                e += powerSmooth[(size_t) b] * bandKWeight[(size_t) b] * std::pow (10.0, (bandGainDb[(size_t) b] - cut) / 10.0);
            }
            return e;
        };

        double c = 0.0;
        const double eRaw = energyWith (0.0);
        if (eRaw > eBefore && uMax > 0.0)
        {
            double lo = 0.0, hi = maxCompCut;
            for (int it = 0; it < 14; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                (energyWith (mid) > eBefore ? lo : hi) = mid;
            }
            c = hi;
        }
        for (int b = 0; b < numBands; ++b)
        {
            const double target = uMax > 0.0 ? std::min (c * scratchU[(size_t) b] / uMax, maxCompCut) : 0.0;
            bandCompDb[(size_t) b] = compCoef * bandCompDb[(size_t) b] + (1.0 - compCoef) * target;
        }
        // Rest global (z. B. wenn Tame Energie entfernt hat oder das Budget nicht reicht)
        double eNow = 0.0;
        for (int b = 0; b < numBands; ++b)
            eNow += powerSmooth[(size_t) b] * bandKWeight[(size_t) b] * std::pow (10.0, (bandGainDb[(size_t) b] - bandCompDb[(size_t) b]) / 10.0);
        const double comp = (eBefore > 0.0 && eNow > 0.0) ? 10.0 * std::log10 (eBefore / eNow) : 0.0;
        loudComp = compCoef * loudComp + (1.0 - compCoef) * std::clamp (comp, -6.0, 6.0);

        for (int b = 0; b < numBands; ++b)
            bandTotal[(size_t) b] = bandGainDb[(size_t) b] - bandCompDb[(size_t) b];

        applyBandGains();
    }

    void applyBandGains()
    {
        // Band-Gains (an den Bandmitten) linear in Bark auf die Bins interpolieren
        const auto& gdb = bandTotal;
        for (int k = 0; k <= kFftSize / 2; ++k)
        {
            const double z = binBark[(size_t) k];
            double g;
            if (z <= bandZ.front()) g = gdb.front();
            else if (z >= bandZ.back()) g = gdb.back();
            else
            {
                const double pos = (z - bandZ.front()) / 0.5;
                const int i = std::min (numBands - 2, (int) std::floor (pos));
                const double a = pos - i;
                g = gdb[(size_t) i] * (1.0 - a) + gdb[(size_t) (i + 1)] * a;
            }
            binGain[(size_t) k] = std::pow (10.0, (g + loudComp) / 20.0);
        }
    }

    static constexpr double kPiC = 3.14159265358979323846;
    double fs = 48000.0, amount = 0.5;

    std::vector<std::complex<double>> twiddle, buf;
    std::vector<int> bitrev;
    std::vector<double> window;

    std::vector<float> inFifo, outFifo, accum;
    int rover = 0;

    int numBands = 0;
    std::vector<int> bandOfBin, bandCount;
    std::vector<double> binBark, bandZ, bandAth, spread, power, binGain, bandGainDb;
    std::vector<double> scratchLvl, scratchTarget, scratchRec, maskWeight;
    double powerNorm = 1.0;
    double gainCoef = 0.0, compCoef = 0.0, powCoef = 0.0, loudComp = 0.0;
    std::vector<double> powerSmooth, bandKWeight, bandLogSum, bandAlpha, binPow, awindow, flatWeight,
                        bandPresence, bandCompDb, bandCompTarget, bandTotal, scratchU;
    std::vector<std::complex<double>> abuf;
    double lastRecover = 0.0, lastTame = 0.0;
};

} // namespace ironcurve
