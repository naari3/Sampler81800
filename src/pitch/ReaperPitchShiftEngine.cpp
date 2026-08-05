// SDK 隔離: このTUは JUCE を含めない。windows.h のマクロ汚染を抑止する。
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
#endif

#include "host/reaper_sdk/reaper_plugin.h"   // ReaSample(double), IReaperPitchShift, REAPER_PITCHSHIFT_API_VER

#include "ReaperPitchShiftEngine.h"
#include "host/ReaperApi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>   // ★デバッグログ（実機検証用・後で削除）

namespace otomad
{

using ReaperGetPitchShiftAPI_t = IReaperPitchShift* (*) (int version);

ReaperPitchShiftEngine::~ReaperPitchShiftEngine() { destroyShifter(); }

void ReaperPitchShiftEngine::destroyShifter() noexcept
{
    if (pitchShift != nullptr)
    {
        delete static_cast<IReaperPitchShift*> (pitchShift);
        pitchShift = nullptr;
    }
}

void ReaperPitchShiftEngine::prepare (const PitchEngineContext& ctx, EngineResources&)
{
    sampleRate = ctx.sampleRate;
    numCh      = std::max (1, ctx.numChannels);
    pullScratch.assign ((std::size_t) (ctx.maxBlockSize + 8) * (std::size_t) numCh, 0.0);

    destroyShifter();
    latency = 0;

    if (api == nullptr || ! api->isAvailable())
        return;   // 非REAPER → isAvailable()==false のまま（Voice が代替へ）

    auto getPS = reinterpret_cast<ReaperGetPitchShiftAPI_t> (api->getFunction ("ReaperGetPitchShiftAPI"));
    if (getPS == nullptr)
        return;

    IReaperPitchShift* ps = getPS (REAPER_PITCHSHIFT_API_VER);
    if (ps == nullptr)
        return;

    ps->set_srate (sampleRate);
    ps->set_nch (numCh);
    ps->set_shift (1.0);
    ps->set_tempo (1.0);
    ps->Reset();
    pitchShift = ps;

    // 簡易レイテンシ・プロービング（無音を流し最初の出力が出るまでのフレーム数を実測, §5.4）
    // ★ in-REAPER 実測。上限を設けて暴走を防ぐ。
    const int probeChunk = 256;
    int fed = 0;
    latency = 0;
    std::vector<double> tmp ((std::size_t) probeChunk * (std::size_t) numCh, 0.0);
    for (int guard = 0; guard < 512; ++guard)
    {
        if (ReaSample* buf = ps->GetBuffer (probeChunk))
        {
            std::memset (buf, 0, sizeof (ReaSample) * (std::size_t) probeChunk * (std::size_t) numCh);
            ps->BufferDone (probeChunk);
            fed += probeChunk;
        }
        const int got = ps->GetSamples (probeChunk, tmp.data());
        if (got > 0) { break; }
        latency = fed;
        if (fed > (int) sampleRate) break;   // 1秒でも出なければ諦める
    }
    ps->Reset();

    // ★デバッグ: 実測レイテンシをログ（メッセージスレッド・後で削除）
    {
        std::ofstream f ("C:/Users/biboo/otomad_reaper_dbg.txt", std::ios::app);
        f << "prepare: probed latency=" << latency
          << " sr=" << sampleRate << " nch=" << numCh << "\n";
    }
}

void ReaperPitchShiftEngine::reset()
{
    lastShift = -1.0;
    lastTempo = -1.0;
    if (pitchShift != nullptr)
        static_cast<IReaperPitchShift*> (pitchShift)->Reset();
}

void ReaperPitchShiftEngine::process (SourceReader& src, double& srcPos,
                                      float* const* out, int numChannels, int n,
                                      const float* pitchRatio, double timeRatio)
{
    const int nch = std::min (numChannels, numCh);

    if (pitchShift == nullptr)   // 使えない場合は無音（本来 Voice が呼ばない）
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::memset (out[ch], 0, sizeof (float) * (std::size_t) n);
        return;
    }

    auto* ps = static_cast<IReaperPitchShift*> (pitchShift);
    // パラメータは変化時のみ設定（毎ブロック set するとモードによりグリッチしうる）
    const double shift = (double) pitchRatio[0];        // ブロック単位（サンプル精度でない, §5.4）
    const double tempo = timeRatio > 0.0 ? timeRatio : 1.0;
    if (shift != lastShift) { ps->set_shift (shift); lastShift = shift; }
    if (tempo != lastTempo) { ps->set_tempo (tempo); lastTempo = tempo; }

    const int srcCh = std::max (1, src.getNumChannels());

    // このブロックで消費すべき入力量（= n × tempo）だけを供給する。
    // 「n出力できるまで供給し続ける」と大レイテンシ時に srcPos が暴走して周期的な無音を生む。
    int toFeed = (int) std::llround ((double) n * tempo);
    if (toFeed < 1)
        toFeed = 1;

    int fed = 0;
    while (fed < toFeed)
    {
        const int chunk = std::min (toFeed - fed, 512);
        ReaSample* buf = ps->GetBuffer (chunk);
        if (buf == nullptr)
            break;
        for (int i = 0; i < chunk; ++i)
        {
            const long idx = (long) std::floor (srcPos);
            for (int ch = 0; ch < numCh; ++ch)
                buf[(std::size_t) (i * numCh + ch)] = (ReaSample) src.sampleAt (std::min (ch, srcCh - 1), idx);
            srcPos += 1.0;   // 入力1フレーム = ソース1サンプル
        }
        ps->BufferDone (chunk);
        fed += chunk;
    }

    // 出せるだけ取り出す。レイテンシ期間は n に満たない → 残りは無音（ホストが PDC 補償）。
    int produced = ps->GetSamples (n, pullScratch.data());
    if (produced > n) produced = n;
    if (produced < 0) produced = 0;

    for (int i = 0; i < produced; ++i)
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] = (float) pullScratch[(std::size_t) (i * numCh + ch)];

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = produced; i < n; ++i)
            out[ch][i] = 0.0f;
}

} // namespace otomad
