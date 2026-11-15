// Ironcurve Amp - Box-Klangkurve der Gitarre (eigene Lautsprecher-Abstimmung)
// Frequenzraster: 20 Hz * 2^(i/12), i = 0..120 (1/12 Oktave). Werte in dB, 0 dB bei 1 kHz.
#pragma once
namespace ironcurve
{
constexpr int kCabCurvePoints = 121;
constexpr float kCabCurveDb[kCabCurvePoints] = { -12.7f, -11.4f, -10.0f, -8.6f, -7.2f, -5.8f, -4.4f, -3.0f, -1.7f, -0.3f, 1.1f, 2.5f, 3.9f, 4.0f, 5.3f, 6.1f, 7.9f, 8.9f, 9.5f, 11.1f, 16.0f, 15.9f, 16.2f, 17.1f, 18.5f, 22.2f, 26.7f, 32.3f, 31.7f, 30.8f, 27.5f, 21.1f, 16.4f, 16.1f, 16.1f, 16.0f, 15.5f, 15.6f, 14.1f, 8.0f, 6.0f, 5.9f, 6.4f, 10.0f, 2.0f, 2.2f, 2.2f, 2.1f, 6.3f, 6.7f, 5.2f, 3.1f, 0.8f, -0.6f, -2.0f, -1.2f, -3.3f, -4.3f, -3.0f, -2.9f, 0.1f, 1.4f, 2.0f, 2.9f, 3.5f, 3.8f, 2.6f, 2.2f, 0.0f, 0.6f, 3.1f, 3.5f, 4.4f, 3.4f, 1.2f, 1.1f, 1.7f, 3.7f, 4.9f, 6.1f, 8.1f, 8.7f, 9.8f, 9.6f, 9.6f, 8.8f, 7.3f, 8.4f, 8.3f, 9.0f, 9.7f, 9.3f, 9.8f, 10.2f, 10.4f, 10.3f, 10.5f, 10.2f, 9.8f, 9.3f, 6.8f, 5.0f, 4.1f, -0.3f, -1.7f, -2.6f, -3.3f, -2.1f, -1.0f, -1.8f, -1.8f, -2.5f, -4.1f, -5.0f, -8.4f, -7.4f, -7.7f, -15.2f, -15.6f, -15.3f, -15.2f };
}
