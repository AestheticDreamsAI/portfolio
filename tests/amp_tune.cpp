// Abstimm-Werkzeug: amp_tune in.f32 out.f32 fs  p0 p1 ... (22 Werte)  -> verarbeitet Mono-Rohdaten
#include "../Source/DSP/AmpV2.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace ironcurve;
int main (int argc, char** argv)
{
    if (argc < 4) return 2;
    FILE* f = std::fopen (argv[1], "rb"); if (! f) return 3;
    std::fseek (f, 0, SEEK_END); const long n = std::ftell (f) / 4; std::fseek (f, 0, SEEK_SET);
    std::vector<float> x ((size_t) n); if (std::fread (x.data(), 4, (size_t) n, f) != (size_t) n) return 4; std::fclose (f);
    AmpVoice v; double* p[] = { &v.boostHpHz, &v.boostMidHz, &v.boostMidDb, &v.boostMidQ, &v.boostLpHz,
        &v.g1Db, &v.hp1Hz, &v.lp1Hz, &v.bias1, &v.g2Db, &v.hp2Hz, &v.lp2Hz, &v.bias2,
        &v.clipDb, &v.clipKnee, &v.clipAsym, &v.hp3Hz, &v.lp3Hz, &v.powerDb, &v.powerBias, &v.powerKnee, &v.outDb, &v.gridAmt, &v.gridTauMs, &v.pre[0], &v.pre[1], &v.pre[2], &v.pre[3], &v.tone[0], &v.tone[1], &v.tone[2], &v.sagAmt, &v.sagRelMs, &v.noiseDb, &v.lowXHz, &v.lowDriveDb, &v.lowLevelDb };
    for (int i = 0; i < 37 && 4 + i < argc; ++i) *p[i] = std::atof (argv[4 + i]);
    GuitarAmpV2 a; a.prepare (std::atof (argv[3])); a.setVoice (v);
    for (auto& s : x) s = (float) a.process (s);
    f = std::fopen (argv[2], "wb"); std::fwrite (x.data(), 4, (size_t) n, f); std::fclose (f);
    return 0;
}
