#include "WsolaEngine.h"
#include "pitch/dsp/Interpolators.h"

#include <algorithm>
#include <cmath>

namespace otomad
{

void WsolaEngine::prepare (const PitchEngineContext&, EngineResources& r)
{
    res     = &r;
    frame   = r.wsolaFrame;
    hop     = r.wsolaHop;
    search  = r.wsolaSearch;
    overlap = frame - hop;
    hann    = r.hannWsola.data();
    prepCh  = 2;
    cap     = (long) frame * 4;

    acc.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) frame, 0.0f));
    accW.assign ((std::size_t) frame, 0.0f);
    intRing.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) cap, 0.0f));
    templateBuf.assign ((std::size_t) overlap, 0.0f);

    reset();
}

void WsolaEngine::reset()
{
    needInit    = true;
    firstFrame  = true;
    analysisPos = 0.0;
    intReadPos  = 0.0;
    intWrite    = 0;
    for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0f);
    std::fill (accW.begin(), accW.end(), 0.0f);
    for (auto& r : intRing) std::fill (r.begin(), r.end(), 0.0f);
    std::fill (templateBuf.begin(), templateBuf.end(), 0.0f);
}

float WsolaEngine::intAt (int ch, long idx) const noexcept
{
    if (idx < 0 || idx >= intWrite || idx < intWrite - cap)
        return 0.0f;
    return intRing[(std::size_t) ch][(std::size_t) (idx % cap)];
}

void WsolaEngine::synthesizeFrame (SourceReader& src, double pitchRatio, double timeRatio) noexcept
{
    const int nch = std::min (prepCh, std::max (1, src.getNumChannels()));
    const double hopAna = (double) hop * timeRatio / std::max (1.0e-6, pitchRatio);

    const long center = std::lround (analysisPos);

    // ---- δ 探索: ch0 を templateBuf に対して正規化相互相関で合わせる ----
    int bestDelta = 0;
    if (! firstFrame)
    {
        float best = -1.0e30f;
        constexpr int step = 8;
        for (int d = -search; d <= search; ++d)
        {
            float dot = 0.0f, energy = 0.0f;
            for (int k = 0; k < overlap; k += step)
            {
                const float sv = src.sampleAt (0, center + d + k);
                dot    += sv * templateBuf[(std::size_t) k];
                energy += sv * sv;
            }
            const float score = dot / std::sqrt (energy + 1.0e-9f);
            if (score > best) { best = score; bestDelta = d; }
        }
    }
    const long start = center + bestDelta;

    // ---- 窓かけ OLA ----
    for (int ch = 0; ch < nch; ++ch)
    {
        auto& a = acc[(std::size_t) ch];
        for (int k = 0; k < frame; ++k)
            a[(std::size_t) k] += src.sampleAt (ch, start + k) * hann[k];
    }
    for (int k = 0; k < frame; ++k)
        accW[(std::size_t) k] += hann[k];

    // ---- 先頭 hop サンプルを確定して中間ストリームへ ----
    for (int k = 0; k < hop; ++k)
    {
        const long idx = intWrite + k;
        const float den = accW[(std::size_t) k];
        for (int ch = 0; ch < nch; ++ch)
        {
            const float v = den > 1.0e-6f ? acc[(std::size_t) ch][(std::size_t) k] / den : 0.0f;
            intRing[(std::size_t) ch][(std::size_t) (idx % cap)] = v;
        }
        // 使わないchは0で埋める
        for (int ch = nch; ch < prepCh; ++ch)
            intRing[(std::size_t) ch][(std::size_t) (idx % cap)] = 0.0f;
    }
    intWrite += hop;

    // ---- acc/accW を hop 分シフト（末尾を0クリア）----
    for (int ch = 0; ch < prepCh; ++ch)
    {
        auto& a = acc[(std::size_t) ch];
        std::copy (a.begin() + hop, a.end(), a.begin());
        std::fill (a.end() - hop, a.end(), 0.0f);
    }
    std::copy (accW.begin() + hop, accW.end(), accW.begin());
    std::fill (accW.end() - hop, accW.end(), 0.0f);

    // ---- template = 自然な続き（選んだフレームの hop 先）----
    for (int k = 0; k < overlap; ++k)
        templateBuf[(std::size_t) k] = src.sampleAt (0, start + hop + k);

    firstFrame = false;
    analysisPos += hopAna;
}

void WsolaEngine::process (SourceReader& src, double& srcPos,
                           float* const* out, int numChannels, int n,
                           const float* pitchRatio, double timeRatio)
{
    if (needInit)
    {
        analysisPos = srcPos;
        needInit = false;
    }

    const int nch = std::min (numChannels, prepCh);

    for (int i = 0; i < n; ++i)
    {
        const double pr = (double) pitchRatio[i];

        while (intWrite < (long) std::floor (intReadPos) + 2)
            synthesizeFrame (src, pr, timeRatio);

        const long   i0 = (long) std::floor (intReadPos);
        const float  t  = (float) (intReadPos - (double) i0);
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] = dsp::hermite4 (intAt (ch, i0 - 1), intAt (ch, i0),
                                        intAt (ch, i0 + 1), intAt (ch, i0 + 2), t);

        intReadPos += pr;
    }

    srcPos = analysisPos;   // 論理位置を書き戻す（エンジン切替で保たれる, §4.1）
}

} // namespace otomad
