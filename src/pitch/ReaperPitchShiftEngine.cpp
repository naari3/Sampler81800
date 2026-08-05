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
    int produced = 0;
    int guard = 0;
    // レイテンシの大きいモード(élastique 等)を1ブロックで充填できるよう十分大きく取る。
    const int guardMax = (n + 65536) / 64 + 64;

    while (produced < n && guard++ < guardMax)
    {
        // 1) まず取り出せるだけ取り出す（FlushSamples はストリーム途中では呼ばない）
        const int got = ps->GetSamples (n - produced, pullScratch.data());
        if (got > 0)
        {
            for (int i = 0; i < got && produced + i < n; ++i)
                for (int ch = 0; ch < nch; ++ch)
                    out[ch][produced + i] = (float) pullScratch[(std::size_t) (i * numCh + ch)];
            produced += got;
            continue;
        }

        // 2) 足りない → 入力を供給（レイテンシ充填はここで素直に進む）
        const int chunk = 128;
        ReaSample* buf = ps->GetBuffer (chunk);
        if (buf == nullptr)
            break;
        for (int i = 0; i < chunk; ++i)
        {
            const long idx = (long) std::floor (srcPos);
            for (int ch = 0; ch < numCh; ++ch)
                buf[(std::size_t) (i * numCh + ch)] = (ReaSample) src.sampleAt (std::min (ch, srcCh - 1), idx);
            srcPos += timeRatio;   // 長さ保持系: 入力消費は timeRatio
        }
        ps->BufferDone (chunk);
    }

    // 埋まらなかった残りは無音
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = produced; i < n; ++i)
            out[ch][i] = 0.0f;
}

} // namespace otomad
