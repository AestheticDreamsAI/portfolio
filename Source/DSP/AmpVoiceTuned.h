// Ironcurve Amp - abgestimmter Grundklang (automatisch erzeugt aus der Abstimmung)
#pragma once
namespace ironcurve
{
inline AmpVoice tunedVoice()
{
    AmpVoice v;
    v.boostHpHz = 87.31851;
    v.boostMidHz = 1740.43791;
    v.boostMidDb = 0.66544;
    v.boostMidQ = 1.09068;
    v.boostLpHz = 8494.36636;
    v.g1Db = 25.23254;
    v.hp1Hz = 10.76954;
    v.lp1Hz = 3710.64964;
    v.bias1 = -0.17051;
    v.g2Db = 33.56577;
    v.hp2Hz = 252.35248;
    v.lp2Hz = 7967.53566;
    v.bias2 = 0.36831;
    v.clipDb = -2.39527;
    v.clipKnee = 3.79272;
    v.clipAsym = 0.12737;
    v.hp3Hz = 28.86197;
    v.lp3Hz = 11221.46282;
    v.powerDb = 17.70747;
    v.powerBias = 0.04387;
    v.powerKnee = 1.51175;
    v.gridAmt = 0.00762;
    v.gridTauMs = 4.74859;
    v.sagAmt = 0.00048;
    v.sagRelMs = 62.69879;
    v.noiseDb = -82.26726;
    v.lowXHz = 247.83987;
    v.lowDriveDb = 16.60551;
    v.lowLevelDb = -35.97948;
    v.pre[0] = 7.4692;
    v.pre[1] = -1.7761;
    v.pre[2] = 8.1008;
    v.pre[3] = -0.5635;
    v.tone[0] = -11.8825;
    v.tone[1] = -10.9601;
    v.tone[2] = -9.4360;
    v.outDb = -17.8;
    return v;
}
}
