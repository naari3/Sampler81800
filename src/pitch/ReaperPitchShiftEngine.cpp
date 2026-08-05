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
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>   // ★デバッグログ（実機検証用・後で削除）

namespace otomad
{

using ReaperGetPitchShiftAPI_t = IReaperPitchShift* (*) (int version);
using EnumModes_t    = bool (*) (int mode, const char** nameOut);
using EnumSubModes_t = const char* (*) (int mode, int submode);

// 無音を流し、最初の出力が出るまでに供給したフレーム数＝レイテンシを実測する。
static int probeLatencyOf (IReaperPitchShift* ps, int numCh, double sr) noexcept
{
    ps->Reset();
    const int probeChunk = 256;
    std::vector<double> tmp ((std::size_t) probeChunk * (std::size_t) numCh, 0.0);
    int fed = 0, lat = 0;
    for (int g = 0; g < 4096; ++g)
    {
        if (ReaSample* buf = ps->GetBuffer (probeChunk))
        {
            std::memset (buf, 0, sizeof (ReaSample) * (std::size_t) probeChunk * (std::size_t) numCh);
            ps->BufferDone (probeChunk);
            fed += probeChunk;
        }
        if (ps->GetSamples (probeChunk, tmp.data()) > 0) break;
        lat = fed;
        if (fed > 2 * (int) sr) break;
    }
    ps->Reset();
    return lat;
}

ReaperPitchShiftEngine::~ReaperPitchShiftEngine() { destroyShifter(); }

void ReaperPitchShiftEngine::buildModeList()
{
    modeEncodings.clear();
    if (api == nullptr)
        return;
    auto enumModes = reinterpret_cast<EnumModes_t>    (api->getFunction ("EnumPitchShiftModes"));
    auto enumSub   = reinterpret_cast<EnumSubModes_t> (api->getFunction ("EnumPitchShiftSubModes"));
    if (enumModes == nullptr)
        return;

    // 一度だけ、全モードのレイテンシを実測してログ（どれが低遅延か一覧化）
    static std::atomic<bool> logged { false };
    const bool doLog = ! logged.exchange (true);
    std::ofstream f;
    if (doLog)
    {
        f.open ("C:/Users/biboo/otomad_reaper_dbg.txt", std::ios::app);
        f << "--- REAPER pitch modes (index : name : latency) ---\n";
    }
    auto* ps = static_cast<IReaperPitchShift*> (pitchShift);

    const char* mn = nullptr;
    int idx = 0;
    for (int mode = 0; enumModes (mode, &mn); ++mode)
    {
        const std::string mname = mn ? mn : "?";
        if (enumSub != nullptr)
        {
            for (int sm = 0; ; ++sm)
            {
                const char* sn = enumSub (mode, sm);
                if (sn == nullptr) break;
                const int enc = (mode << 16) + sm;
                modeEncodings.push_back (enc);
                if (doLog && ps != nullptr)
                {
                    ps->SetQualityParameter (enc);
                    f << idx << " : " << mname << " / " << sn
                      << " : lat=" << probeLatencyOf (ps, numCh, sampleRate) << "\n";
                }
                ++idx;
            }
        }
        else
        {
            const int enc = (mode << 16);
            modeEncodings.push_back (enc);
            if (doLog && ps != nullptr)
                f << idx << " : " << mname
                  << " : lat=" << probeLatencyOf (ps, numCh, sampleRate) << "\n";
            ++idx;
        }
    }
    if (doLog)
        f << "--- set 'R.Mode' param to the index above (低い lat が低遅延) ---\n";
}

void ReaperPitchShiftEngine::applyMode (int flatIndex)
{
    auto* ps = static_cast<IReaperPitchShift*> (pitchShift);
    if (ps == nullptr)
        return;
    const int enc = (flatIndex >= 0 && flatIndex < (int) modeEncodings.size())
                        ? modeEncodings[(std::size_t) flatIndex] : -1;   // -1 = プロジェクト既定
    ps->SetQualityParameter (enc);
    latency = probeLatencyOf (ps, numCh, sampleRate);
    lastShift = -1.0;
    lastTempo = -1.0;
}

void ReaperPitchShiftEngine::reconfigure()
{
    if (pitchShift != nullptr)
        applyMode (subMode);
}

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
    maxBlock   = std::max (1, ctx.maxBlockSize);
    numCh      = std::max (1, ctx.numChannels);
    pullScratch.assign ((std::size_t) (maxBlock + 8) * (std::size_t) numCh, 0.0);

    fifoCap = maxBlock + 16384;
    fifo.assign ((std::size_t) fifoCap * (std::size_t) numCh, 0.0);
    fifoRead = fifoWrite = fifoCount = 0;

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

    buildModeList();     // モード一覧を構築（初回のみ全モードのレイテンシをログ）
    applyMode (subMode); // 選択モードを適用＋レイテンシ実測
}

void ReaperPitchShiftEngine::reset()
{
    lastShift = -1.0;
    lastTempo = -1.0;
    fifoRead = fifoWrite = fifoCount = 0;
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

    // 取り出せるものは全部 FIFO へ（フレーム粒度でブロック内に過不足が出ても FIFO が吸収）。
    for (int g = 0; g < 64; ++g)
    {
        const int room = fifoCap - fifoCount;
        if (room < maxBlock) break;
        const int got = ps->GetSamples (maxBlock, pullScratch.data());
        if (got <= 0) break;
        for (int i = 0; i < got; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
                fifo[(std::size_t) (fifoWrite * numCh + ch)] = pullScratch[(std::size_t) (i * numCh + ch)];
            fifoWrite = (fifoWrite + 1) % fifoCap;
        }
        fifoCount += got;
    }

    // FIFO から n サンプル出力（不足はレイテンシ充填期間のみ→無音, ホストが PDC 補償）。
    const int avail = std::min (n, fifoCount);
    for (int i = 0; i < avail; ++i)
    {
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] = (float) fifo[(std::size_t) (fifoRead * numCh + ch)];
        fifoRead = (fifoRead + 1) % fifoCap;
    }
    fifoCount -= avail;

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = (ch < nch ? avail : 0); i < n; ++i)
            out[ch][i] = 0.0f;
}

} // namespace otomad
