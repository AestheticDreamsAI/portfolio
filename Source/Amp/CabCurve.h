// Ironcurve Amp - Box-Klangkurve der Gitarre (eigene Lautsprecher-Abstimmung)
// Frequenzraster: 20 Hz * 2^(i/12), i = 0..120 (1/12 Oktave). Werte in dB, 0 dB bei 1 kHz.
#pragma once
namespace ironcurve
{
constexpr int kCabCurvePoints = 121;
constexpr float kCabCurveDb[kCabCurvePoints] = { -34.1f, -32.7f, -31.3f, -29.9f, -28.5f, -27.1f, -25.8f, -24.4f, -23.0f, -21.6f, -20.9f, -21.0f, -20.6f, -19.6f, -16.1f, -11.4f, -7.7f, -5.7f, -4.0f, -2.9f, -2.8f, -1.9f, -0.9f, 2.3f, 6.7f, 9.6f, 13.9f, 17.2f, 17.4f, 17.2f, 16.5f, 14.5f, 14.3f, 12.8f, 12.2f, 9.6f, 9.8f, 9.4f, 8.5f, 5.7f, 5.1f, 4.4f, 2.3f, 4.7f, 5.0f, 6.6f, 5.9f, 4.5f, 3.9f, 1.7f, 0.5f, -0.1f, -0.7f, -1.3f, -2.3f, -3.7f, -4.8f, -5.8f, -4.6f, -3.4f, -1.1f, -0.2f, 0.5f, 0.7f, 0.7f, 1.1f, 0.5f, 0.7f, 0.0f, 0.3f, 2.7f, 2.7f, 3.4f, 3.0f, 0.9f, 0.9f, 1.7f, 2.7f, 4.3f, 5.3f, 6.0f, 7.0f, 7.6f, 7.5f, 7.5f, 6.8f, 5.4f, 6.7f, 6.7f, 7.4f, 7.9f, 7.6f, 8.0f, 8.3f, 8.5f, 8.2f, 8.2f, 7.5f, 6.9f, 6.2f, 3.4f, 1.4f, 0.0f, -4.9f, -6.8f, -8.2f, -9.4f, -8.8f, -8.4f, -9.9f, -11.4f, -12.9f, -14.6f, -16.2f, -20.4f, -20.3f, -21.5f, -29.8f, -31.1f, -31.6f, -31.9f };
}
