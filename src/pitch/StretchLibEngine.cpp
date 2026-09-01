#include "StretchLibEngine.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>

namespace otomad
{

struct StretchLibEngine::Impl
{
    // 乱数シードを固定する。既定コンストラクタは std::random_device を引くので、
    // 同じ入力でも実行ごとに出力が変わり、ブロック分割不変性を検証できなくなる。
    signalsmith::stretch::SignalsmithStretch<float> stretch { 20240101L };
};

StretchLibEngine::StretchLibEngine() : impl (std::make_unique<Impl>()) {}
StretchLibEngine::~StretchLibEngine() = default;

void StretchLibEngine::prepare (const PitchEngineContext& ctx, EngineResources&)
{
    prepCh   = 2;
    maxBlock = std::max (64, ctx.maxBlockSize);

    // presetDefault は blockSamples = 0.12*sr（48kHz で 5760 = 120ms）で、レイテンシが
    // そのまま 120ms になり演奏用途には大きすぎる。ブロック長を直接指定して詰める。
    // 実測（レイテンシはブロック長に比例し、報告値は常に実体と一致した）。
    // ピッチ誤差は**単調ではない**ので、短くするほど悪化すると決めつけないこと:
    //   block  レイテンシ   +7半音の誤差(等倍/2倍長)
    //   0.12    120ms       +0.0 / +1.0
    //   0.10    100ms       +2.2 / +5.4
    //   0.08     80ms       +4.9 / +6.4
    //   0.06     60ms       -0.9 / -0.3   ← 採用
    //   0.05     50ms       +8.1 / +10.4
    // -12〜+12半音で確認しても 0.06 は 0.12 と同等（最悪 8.3ct vs 7.2ct）。
    // レイテンシだけ半分にできるのでここを使う。0.05 へ詰めると精度が落ちるので下げない。
    impl->stretch.configure (prepCh,
                             (int) (ctx.sampleRate * 0.06),     // block   → 60ms
                             (int) (ctx.sampleRate * 0.015));   // interval
    latency = impl->stretch.inputLatency() + impl->stretch.outputLatency();

    // timeRatio は Manual(0.25..4x) と Sync でかなり動く。1ブロックあたりの入力は
    // maxBlock * timeRatio なので、余裕を持って 8 倍まで一気に読めるようにしておく。
    // これを超える比率のときは process 側で出力を分割する。
    cap = maxBlock * 8 + 64;
    inBuf.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) cap, 0.0f));
    dumpBuf.assign ((std::size_t) cap, 0.0f);
    inPtr.assign ((std::size_t) prepCh, nullptr);
    outPtr.assign ((std::size_t) prepCh, nullptr);

    // ウォームアップ: process 内の一時バッファは初回呼び出しで確保される。
    // 音声スレッドで確保させないため、ここで無音を通して容量を作りきる（規約1）。
    {
        std::vector<std::vector<float>> warmOut ((std::size_t) prepCh,
                                                 std::vector<float> ((std::size_t) maxBlock, 0.0f));
        std::vector<const float*> ip ((std::size_t) prepCh);
        std::vector<float*>       op ((std::size_t) prepCh);
        for (int ch = 0; ch < prepCh; ++ch)
        { ip[(std::size_t) ch] = inBuf[(std::size_t) ch].data(); op[(std::size_t) ch] = warmOut[(std::size_t) ch].data(); }

        for (int i = 0; i < 4; ++i)
            impl->stretch.process (ip.data(), cap - 64, op.data(), maxBlock);
    }

    reset();
}

void StretchLibEngine::reset()
{
    impl->stretch.reset();
    needInit = true;
    srcRead  = 0.0;
}

void StretchLibEngine::process (SourceReader& src, double& srcPos,
                                float* const* out, int numChannels, int n,
                                const float* pitchRatio, double timeRatio)
{
    if (needInit)
    {
        srcRead  = srcPos;
        needInit = false;
    }

    const int nch = std::min (numChannels, prepCh);
    const int sch = std::max (1, src.getNumChannels());

    // 規約10: ピッチはブロック先頭値で固定する（フレーム系と同じ扱い）。
    const double pr = n > 0 ? (double) pitchRatio[0] : 1.0;
    impl->stretch.setTransposeFactor ((float) std::max (1.0e-6, pr));

    const double tr = std::max (1.0e-6, timeRatio);

    // 1回で読む入力が cap を超えないように出力を分割する。
    const int maxOutChunk = std::max (1, (int) ((double) (cap - 8) / std::max (1.0, tr)));

    int done = 0;
    while (done < n)
    {
        const int nn = std::min (maxOutChunk, n - done);

        // この塊で消費する入力サンプル数。端数は srcRead に持ち越すので、
        // 平均すると出力1サンプルあたり入力が timeRatio 進む（IPitchEngine の契約）。
        const double wantEnd = srcRead + (double) nn * tr;
        const auto   from    = (std::int64_t) std::floor (srcRead);
        const auto   to      = (std::int64_t) std::floor (wantEnd);
        const int    inN     = (int) std::clamp<std::int64_t> (to - from, 0, cap);

        for (int ch = 0; ch < prepCh; ++ch)
        {
            auto& b = inBuf[(std::size_t) ch];
            const int rch = std::min (ch, sch - 1);      // モノ素材はそのまま両chへ
            for (int i = 0; i < inN; ++i)
                b[(std::size_t) i] = src.sampleAt (rch, from + i);
            inPtr[(std::size_t) ch] = b.data();
        }
        for (int ch = 0; ch < prepCh; ++ch)
            outPtr[(std::size_t) ch] = (ch < nch) ? (out[ch] + done) : nullptr;

        // 未使用chにも書き先が要る（nullptr は渡せない）。**入力バッファは使わない**：
        // 同じ領域を入出力に渡すとエイリアシングになる。専用の捨て先へ逃がす。
        for (int ch = nch; ch < prepCh; ++ch)
            outPtr[(std::size_t) ch] = dumpBuf.data();

        impl->stretch.process (inPtr.data(), inN, outPtr.data(), nn);

        srcRead = wantEnd;
        done   += nn;
    }

    srcPos = srcRead;   // 論理位置を書き戻す（エンジン切替で保たれる, §4.1）
}

} // namespace otomad
