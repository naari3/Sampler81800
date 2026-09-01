#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/SampleBuffer.h"

namespace otomad::test
{

// モノのサイン波 SampleBuffer を作る（data=original=同一, host SR 前提）。
inline SampleBuffer makeSine (double freqHz, double sampleRate, double seconds, float amp = 0.8f)
{
    SampleBuffer sb;
    const std::int64_t n = (std::int64_t) (sampleRate * seconds);
    sb.numChannels       = 1;
    sb.numSamples        = n;
    sb.sampleRate        = sampleRate;
    sb.originalSampleRate = sampleRate;
    sb.name              = "sine";
    sb.data.assign (1, std::vector<float> ((std::size_t) n, 0.0f));

    const double w = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (std::int64_t i = 0; i < n; ++i)
        sb.data[0][(std::size_t) i] = amp * (float) std::sin (w * (double) i);

    sb.original = sb.data;
    return sb;
}

// 立ち上がりゼロクロスを補間して基本周波数を推定する（清浄なサイン向け、<1cent 精度）。
// threshold: ノイズ由来の微小交差を除外する立ち上がり傾きの下限。
inline double estimateF0 (const float* x, int n, double sampleRate, float threshold = 0.01f)
{
    double firstT = -1.0, lastT = -1.0;
    int    count  = 0;
    for (int i = 1; i < n; ++i)
    {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f && (x[i] - x[i - 1]) > threshold)
        {
            const double denom = (double) x[i] - (double) x[i - 1];
            const double frac  = denom != 0.0 ? (double) (-x[i - 1]) / denom : 0.0;
            const double tcr   = ((double) (i - 1) + frac) / sampleRate;
            if (firstT < 0.0)
                firstT = tcr;
            else
            {
                lastT = tcr;
                ++count;
            }
        }
    }
    if (count < 1 || lastT < 0.0)
        return 0.0;
    return (double) count / (lastT - firstT);
}

// 期待周波数の ±40% だけを探索する自己相関で F0 を測る。
// estimateF0（ゼロ交差カウント）は振幅変調のある信号だと交差を数え落として
// オクターブ下を報告するため、Granular のように粒ごとに振幅が揺れる出力には使えない。
inline double estimateF0Near (const float* x, int n, double sampleRate, double wantHz)
{
    auto corr = [&] (int lag)
    {
        double s = 0.0, ea = 0.0, eb = 0.0;
        for (int i = 0; i < n - lag; ++i)
        { const double u = x[i], v = x[i + lag]; s += u * v; ea += u * u; eb += v * v; }
        const double d = std::sqrt (ea * eb);
        return d > 1.0e-12 ? s / d : 0.0;
    };

    const double wantLag = sampleRate / wantHz;
    const int lo = std::max (2, (int) (wantLag * 0.6));
    const int hi = (int) (wantLag * 1.4) + 2;

    double best = -1.0; int bestLag = 0;
    for (int lag = lo; lag <= hi && lag < n / 2; ++lag)
    { const double r = corr (lag); if (r > best) { best = r; bestLag = lag; } }
    if (bestLag <= lo || best < 0.3)
        return 0.0;

    const double y0 = corr (bestLag - 1), y1 = best, y2 = corr (bestLag + 1);
    const double den = y0 - 2.0 * y1 + y2;
    const double dl = std::abs (den) > 1.0e-12 ? 0.5 * (y0 - y2) / den : 0.0;
    return sampleRate / ((double) bestLag + dl);
}

} // namespace otomad::test
