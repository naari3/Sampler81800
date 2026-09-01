#include "PitchDetect.h"

#include <algorithm>
#include <vector>

namespace otomad::pitchdetect
{

// 1窓の YIN。x は DC除去済みのモノ窓。outFreq に基本周波数、戻り値は非周期性(0=完全周期, 小さいほど信頼)。
double yinWindow (const float* x, int W, double sr, double& outFreq) noexcept
{
    outFreq = 0.0;
    const int minTau = std::max (2, (int) (sr / 1500.0));   // 上限 ~1500Hz
    const int maxTau = std::min (W / 2, (int) (sr / 50.0));  // 下限 ~50Hz
    if (maxTau <= minTau)
        return 1.0;

    std::vector<float> dn ((std::size_t) (maxTau + 1), 1.0f);   // 累積平均正規化差分
    float running = 0.0f;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j + tau < W; ++j)
        {
            const float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        running += sum;
        dn[(std::size_t) tau] = running > 0.0f ? sum * (float) (tau - minTau + 1) / running : 1.0f;
    }

    // 標準YIN: 閾値を割る最初の谷まで下って、その谷の底(局所最小)を採る（オクターブ下取りを防ぐ）。
    const float thr = 0.15f;
    int best = -1;
    for (int tau = minTau; tau < maxTau; ++tau)
    {
        if (dn[(std::size_t) tau] < thr)
        {
            while (tau + 1 < maxTau && dn[(std::size_t) (tau + 1)] < dn[(std::size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)   // 閾値未達 → 全体最小
    {
        int mt = minTau; float mv = dn[(std::size_t) minTau];
        for (int tau = minTau + 1; tau <= maxTau; ++tau)
            if (dn[(std::size_t) tau] < mv) { mv = dn[(std::size_t) tau]; mt = tau; }
        best = mt;
    }

    // 放物線補間
    double tauR = best;
    if (best > minTau && best < maxTau)
    {
        const float a = dn[(std::size_t) (best - 1)], b = dn[(std::size_t) best], c = dn[(std::size_t) (best + 1)];
        const float den = a + c - 2.0f * b;
        if (std::abs (den) > 1.0e-9f)
            tauR = best + 0.5 * (double) ((a - c) / den);
    }
    if (tauR <= 0.0)
        return 1.0;
    outFreq = sr / tauR;
    return dn[(std::size_t) best];
}

} // namespace otomad::pitchdetect
