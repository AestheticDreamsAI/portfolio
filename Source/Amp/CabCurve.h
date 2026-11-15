// Ironcurve Amp - Box-Klangkurve der Gitarre (eigene Lautsprecher-Abstimmung)
// Frequenzraster: 20 Hz * 2^(i/12), i = 0..120 (1/12 Oktave). Werte in dB, 0 dB bei 1 kHz.
#pragma once
namespace ironcurve
{
constexpr int kCabCurvePoints = 121;
constexpr float kCabCurveDb[kCabCurvePoints] = { -21.4f, -20.1f, -18.7f, -17.3f, -15.9f, -14.5f, -13.1f, -11.7f, -10.4f, -9.0f, -7.6f, -6.2f, -5.5f, -5.4f, -3.4f, 0.0f, 3.2f, 5.2f, 7.4f, 10.7f, 14.5f, 18.0f, 18.3f, 18.1f, 22.0f, 24.5f, 28.9f, 35.2f, 40.6f, 38.6f, 31.5f, 26.4f, 25.0f, 23.6f, 23.2f, 21.4f, 21.8f, 23.3f, 21.9f, 15.7f, 14.1f, 14.4f, 17.5f, 15.3f, 13.7f, 5.2f, 5.6f, 9.6f, 10.7f, 8.8f, 8.4f, 5.7f, 2.1f, 1.3f, 1.6f, 3.3f, 0.7f, -0.5f, 1.0f, 1.8f, 6.5f, 7.7f, 7.9f, 6.9f, 5.9f, 4.9f, 2.8f, 2.0f, 0.0f, 0.0f, 1.9f, 2.2f, 2.7f, 2.0f, 0.8f, 0.7f, 2.2f, 4.2f, 5.6f, 7.0f, 8.1f, 8.8f, 9.6f, 9.2f, 9.3f, 8.6f, 7.2f, 8.6f, 8.8f, 9.8f, 10.8f, 10.7f, 11.2f, 11.4f, 11.4f, 10.8f, 10.7f, 10.0f, 9.4f, 8.6f, 5.8f, 3.8f, 2.6f, -2.3f, -4.0f, -5.4f, -6.5f, -5.7f, -5.2f, -6.5f, -8.0f, -9.5f, -11.0f, -12.5f, -15.8f, -15.5f, -17.0f, -23.9f, -24.7f, -24.7f, -24.3f };
}
