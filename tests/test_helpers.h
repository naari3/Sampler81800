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

} // namespace otomad::test
