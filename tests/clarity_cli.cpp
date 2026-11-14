// Offline-Werkzeug: Klarheit (+ optional True-Peak-Limiter) auf rohe Stereo-Float32-Daten
//   clarity_cli <samplerate> <amount 0..1> <limit 0/1> < in.f32 > out.f32
// Gibt auf stderr die mittlere Recover-/Tame-Aktivitaet aus. Latenz wird kompensiert.
#include "../Source/DSP/Clarity.h"
#include "../Source/DSP/Dynamics.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 4) return 2;
    const double fs = std::atof (argv[1]);
    const double amount = std::atof (argv[2]);
    const bool limit = std::atoi (argv[3]) != 0;

    std::vector<float> in;
    float buf[4096];
    size_t n;
    while ((n = std::fread (buf, sizeof (float), 4096, stdin)) > 0)
        in.insert (in.end(), buf, buf + n);
    const size_t frames = in.size() / 2;

    ironcurve::Clarity c; c.prepare (fs); c.setAmount (amount);
    ironcurve::Limiter lim; lim.prepare (fs); lim.setCeilingDb (-1.0);
    const size_t lat = (size_t) c.getLatencySamples() + (limit ? (size_t) lim.getLatencySamples() : 0);

    std::vector<float> out (frames * 2, 0.0f);
    double recSum = 0, tameSum = 0; long cnt = 0;
    for (size_t i = 0; i < frames + lat; ++i)
    {
        float l = i < frames ? in[i * 2] : 0.0f, r = i < frames ? in[i * 2 + 1] : 0.0f;
        c.process (l, r);
        if (limit) lim.process (l, r);
        if (i % 512 == 0) { recSum += c.getRecoverDb(); tameSum += c.getTameDb(); ++cnt; }
        if (i >= lat) { out[(i - lat) * 2] = l; out[(i - lat) * 2 + 1] = r; }
    }
    std::fwrite (out.data(), sizeof (float), out.size(), stdout);
    std::fprintf (stderr, "mittlere Aktivitaet: Recover %+.2f dB, Tame %+.2f dB (gemittelt ueber alle Baender)\n",
                  recSum / std::max (1L, cnt), tameSum / std::max (1L, cnt));
    return 0;
}
