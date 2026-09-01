#include "GranularEngine.h"
#include "pitch/dsp/Interpolators.h"

#include <algorithm>
#include <cmath>

namespace otomad
{

void GranularEngine::prepare (const PitchEngineContext&, EngineResources& r)
{
    frame  = r.granFrame;
    hop    = r.granHop;
    jitter = (double) r.granJitter;
    hann   = r.hannGran.data();
    prepCh = 2;
    cap    = (long) frame * 4;

    acc.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) frame, 0.0f));
    accW.assign ((std::size_t) frame, 0.0f);
    outRing.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) cap, 0.0f));

    reset();
}

void GranularEngine::reset()
{
    needInit    = true;
    rng         = 22222u;   // 固定シード: 同じ入力なら常に同じ出力（テスト可能性・規約7）
    analysisPos = 0.0;
    outWrite    = 0;
    outRead     = 0;
    for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0f);
    std::fill (accW.begin(), accW.end(), 0.0f);
    for (auto& r : outRing) std::fill (r.begin(), r.end(), 0.0f);
}

void GranularEngine::synthesizeGrain (SourceReader& src, double pitchRatio, double timeRatio) noexcept
{
    const int nch = std::min (prepCh, std::max (1, src.getNumChannels()));
    const double pr = std::max (1.0e-6, pitchRatio);

    // ---- 粒の中身: 入力を pitchRatio 倍の速さで舐めて窓をかけ、OLA する ----
    // 出力 k サンプル目 ← 入力 (analysisPos + k*pitchRatio)。
    // 粒の中だけ見れば単なる可変速再生なので、周波数はちょうど pitchRatio 倍になる。
    // （粒どうしの位相は揃えないため境界にざらつきが残る。それがグラニュラーの質感）
    // 粒ごとに読み出し位置を少しずらす。ずらさないと粒どうしの位相差が
    //   Δ = ω * hop * (1 - pitchRatio)
    // と規則的に並び、重なり数ぶんの位相が円周上で均等になる比率（例: +7半音・4重ね）で
    // 基音が完全に打ち消される（実測: 440Hz を狙って 407.5Hz になった）。
    // ずらすと位相がばらけて打ち消しが起きず、代わりにわずかな粗さが乗る＝グラニュラーの質感。
    rng = rng * 1664525u + 1013904223u;
    const double jit = jitter * (((double) ((rng >> 8) & 0xFFFFu) / 32767.5) - 1.0);
    const double base = analysisPos + jit;

    for (int k = 0; k < frame; ++k)
    {
        const double  sp = base + (double) k * pr;
        const long    i0 = (long) std::floor (sp);
        const float   t  = (float) (sp - (double) i0);
        const float   w  = hann[k];

        for (int ch = 0; ch < nch; ++ch)
            acc[(std::size_t) ch][(std::size_t) k] +=
                dsp::hermite4 (src.sampleAt (ch, i0 - 1), src.sampleAt (ch, i0),
                               src.sampleAt (ch, i0 + 1), src.sampleAt (ch, i0 + 2), t) * w;
        accW[(std::size_t) k] += w;
    }

    // ---- 先頭 hop サンプルは全ての粒が乗り終わったので確定して出力リングへ ----
    for (int k = 0; k < hop; ++k)
    {
        const long  idx = outWrite + k;
        const float den = accW[(std::size_t) k];
        for (int ch = 0; ch < nch; ++ch)
        {
            const float v = den > 1.0e-6f ? acc[(std::size_t) ch][(std::size_t) k] / den : 0.0f;
            outRing[(std::size_t) ch][(std::size_t) (idx % cap)] = v;
        }
        for (int ch = nch; ch < prepCh; ++ch)   // 使わないchは0で埋める
            outRing[(std::size_t) ch][(std::size_t) (idx % cap)] = 0.0f;
    }
    outWrite += hop;

    // ---- acc/accW を hop 分シフト（末尾を0クリア）----
    for (int ch = 0; ch < prepCh; ++ch)
    {
        auto& a = acc[(std::size_t) ch];
        std::copy (a.begin() + hop, a.end(), a.begin());
        std::fill (a.end() - hop, a.end(), 0.0f);
    }
    std::copy (accW.begin() + hop, accW.end(), accW.begin());
    std::fill (accW.end() - hop, accW.end(), 0.0f);

    // ---- 次の粒の開始位置。ここが「長さ」を決める ----
    // 1粒で出力は hop サンプル進む。そのとき入力を hop*timeRatio だけ進めるので、
    // 出力1サンプルあたり入力は timeRatio 進む ＝ IPitchEngine の契約どおり。
    //   timeRatio=1   → 入力と出力が 1:1（長さそのまま）
    //   timeRatio=0.5 → 入力を半分の速さで舐める（長さ2倍）
    // **pitchRatio では割らない。** 規約5 の式は「時間伸縮→出力段でリサンプル」の
    // 2段構成に対するもので、出力段のリサンプルが pitchRatio ぶん時間を縮めるぶんを
    // 解析hopで先に打ち消す必要があるため割る。本エンジンは出力段のリサンプルを持たず、
    // ピッチは粒の中で完結しているので、時間には一切影響しない。
    analysisPos += (double) hop * timeRatio;
}

void GranularEngine::process (SourceReader& src, double& srcPos,
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
        // 規約10: 粒は「その時点の pitchRatio」で固定して1粒ぶん作る。
        const double pr = (double) pitchRatio[i];

        while (outWrite <= outRead)
            synthesizeGrain (src, pr, timeRatio);

        const auto idx = (std::size_t) (outRead % cap);
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] = outRing[(std::size_t) ch][idx];
        ++outRead;
    }

    srcPos = analysisPos;   // 論理位置を書き戻す（エンジン切替で保たれる, §4.1）
}

} // namespace otomad
