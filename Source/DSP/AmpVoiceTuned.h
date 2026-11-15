// Ironcurve Amp - abgestimmter Grundklang (automatisch erzeugt aus der Abstimmung)
#pragma once
namespace ironcurve
{
inline AmpVoice tunedVoice()
{
    AmpVoice v;
    v.boostHpHz = 122.12922;
    v.boostMidHz = 895.41910;
    v.boostMidDb = 1.51040;
    v.boostMidQ = 0.88852;
    v.boostLpHz = 2753.64047;
    v.g1Db = 38.89375;
    v.hp1Hz = 62.18742;
    v.lp1Hz = 3006.03687;
    v.bias1 = 0.58895;
    v.g2Db = 24.99417;
    v.hp2Hz = 61.19776;
    v.lp2Hz = 3193.61571;
    v.bias2 = 0.53410;
    v.clipDb = -3.74554;
    v.clipKnee = 2.96639;
    v.clipAsym = 0.00191;
    v.hp3Hz = 184.71097;
    v.lp3Hz = 4798.80784;
    v.powerDb = 15.70727;
    v.powerBias = 0.28563;
    v.powerKnee = 4.14817;
    v.gridAmt = 0.62025;
    v.gridTauMs = 2.31229;
    v.sagAmt = 0.03955;
    v.sagRelMs = 125.66448;
    v.pre[0] = 10.8174;
    v.pre[1] = -2.0910;
    v.pre[2] = 2.9912;
    v.pre[3] = -11.3546;
    v.tone[0] = -14.9906;
    v.tone[1] = -11.8407;
    v.tone[2] = -2.6917;
    v.outDb = -21.6;
    return v;
}
}
