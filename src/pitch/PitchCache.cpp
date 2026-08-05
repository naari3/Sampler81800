// SDK 隔離（windows.h のマクロ汚染を抑止, JUCEは含めない）
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
#endif

#include "host/reaper_sdk/reaper_plugin.h"   // IReaperPitchShift, ReaSample(double)

#include "PitchCache.h"
#include "host/ReaperApi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>   // ★診断ログ（実機検証用・後で削除）

namespace otomad
{

using ReaperGetPitchShiftAPI_t = IReaperPitchShift* (*) (int);

bool PitchCache::configure (const SampleBuffer* src, int version, int mode, int sub,
                            double sampleRate, float formant, double timeRatio)
{
    // 微小変化での再レンダリング連発を避けるため量子化
    const float  fq = std::round (formant * 4.0f) / 4.0f;        // 0.25半音刻み
    const double tq = std::round (timeRatio * 100.0) / 100.0;    // 0.01刻み

    std::lock_guard<std::mutex> lock (ownerLock);
    if (src == curSrc && version == curVersion && mode == curMode && sub == curSub
        && std::abs (sampleRate - curSr) < 1.0e-6
        && std::abs (fq - curFormant) < 1.0e-6 && std::abs (tq - curTimeRatio) < 1.0e-6)
        return false;

    // 無効化: 新しい ready のみクリア。古いバッファは再生中の可能性があるので解放しない（graveyard保持）。
    for (auto& p : ready) p.store (nullptr, std::memory_order_release);
    reqLo.store (0); reqHi.store (0);

    curSrc = src; curVersion = version; curMode = mode; curSub = sub; curSr = sampleRate;
    curFormant = fq; curTimeRatio = tq;
    ++curGen;   // 設定が変わった → 進行中のレンダリングは無効化される
    return true;
}

bool PitchCache::renderPending()
{
    int semi = 0; bool found = false;
    {
        // 保留ビットを1つ取り出す
        std::uint64_t lo = reqLo.load();
        for (int b = 0; b < 64 && ! found; ++b)
            if (lo & (1ull << b)) { reqLo.fetch_and (~(1ull << b)); semi = kMin + b; found = true; }
        if (! found)
        {
            std::uint64_t hi = reqHi.load();
            for (int b = 0; b < (kN - 64) && ! found; ++b)
                if (hi & (1ull << b)) { reqHi.fetch_and (~(1ull << b)); semi = kMin + 64 + b; found = true; }
        }
    }
    if (! found)
        return false;

    int usedGen = -1;
    if (auto buf = renderShift (semi, usedGen))
    {
        std::lock_guard<std::mutex> lock (ownerLock);
        if (usedGen == curGen)   // レンダリング中に設定(素材/モード/フォルマント/ストレッチ)が変わっていなければ公開
        {
            graveyard.push_back (buf);
            ready[(std::size_t) (semi - kMin)].store (buf.get(), std::memory_order_release);
        }
    }
    return true;
}

std::shared_ptr<SampleBuffer> PitchCache::renderShift (int semi, int& usedGen)
{
    // 設定のスナップショット
    const SampleBuffer* src; int mode, sub; double sr; float formant; double timeRatio;
    {
        std::lock_guard<std::mutex> lock (ownerLock);
        src = curSrc; mode = curMode; sub = curSub; sr = curSr;
        formant = curFormant; timeRatio = curTimeRatio;
        usedGen = curGen;
    }
    if (api == nullptr || src == nullptr || src->numSamples <= 0)
        return nullptr;

    auto getPS = reinterpret_cast<ReaperGetPitchShiftAPI_t> (api->getFunction ("ReaperGetPitchShiftAPI"));
    if (getPS == nullptr)
        return nullptr;
    IReaperPitchShift* ps = getPS (REAPER_PITCHSHIFT_API_VER);
    if (ps == nullptr)
        return nullptr;

    const int    numCh = std::max (1, src->numChannels);
    const double shift = std::pow (2.0, (double) semi / 12.0);

    ps->set_srate (sr);
    ps->set_nch (numCh);
    ps->set_shift (shift);
    ps->set_formant_shift ((double) formant);            // フォルマントを焼き込む
    ps->set_tempo (timeRatio > 0.0 ? timeRatio : 1.0);   // ストレッチ(長さ)を焼き込む
    ps->SetQualityParameter ((mode << 16) + sub);
    ps->Reset();

    // --- オフライン・レンダリング（プローブ非依存の堅牢版）---
    const std::int64_t n = src->numSamples;
    // 目標出力長（Manualストレッチ等で長さが変わる）。set_tempo(timeRatio) → 出力 ≈ n / timeRatio。
    const std::int64_t expectedLen = timeRatio > 1.0e-6
        ? (std::int64_t) std::llround ((double) n / timeRatio) : n;

    const int chunk = 1024;
    std::vector<double> pull ((std::size_t) chunk * (std::size_t) numCh, 0.0);
    std::vector<std::vector<float>> out ((std::size_t) numCh);
    for (auto& c : out) c.reserve ((std::size_t) (expectedLen + 2 * (std::int64_t) sr));

    auto drain = [&]()
    {
        for (;;)
        {
            const int got = ps->GetSamples (chunk, pull.data());
            if (got <= 0) break;
            for (int i = 0; i < got; ++i)
                for (int ch = 0; ch < numCh; ++ch)
                    out[(std::size_t) ch].push_back ((float) pull[(std::size_t) (i * numCh + ch)]);
        }
    };
    auto outLen = [&]() -> std::int64_t { return out.empty() ? 0 : (std::int64_t) out[0].size(); };

    // 1) 実入力を供給
    for (std::int64_t pos = 0; pos < n; pos += chunk)
    {
        const int c = (int) std::min<std::int64_t> (chunk, n - pos);
        if (ReaSample* b = ps->GetBuffer (c))
        {
            for (int i = 0; i < c; ++i)
                for (int ch = 0; ch < numCh; ++ch)
                    b[(std::size_t) (i * numCh + ch)] = (ReaSample) src->sampleAtRaw (ch, pos + i);
            ps->BufferDone (c);
        }
        drain();
    }

    // 2) 内部に溜まった本体を無音で押し出す（大レイテンシモード対策）。十分な長さになるまで、
    //    または出力がこれ以上増えなくなるまで。
    const std::int64_t maxLead = (std::int64_t) (0.3 * sr);            // 先頭で除去する上限
    const std::int64_t target  = expectedLen + maxLead + (std::int64_t) (0.5 * sr);
    for (int g = 0; g < 200000 && outLen() < target; ++g)
    {
        if (ReaSample* b = ps->GetBuffer (chunk))
        {
            std::memset (b, 0, sizeof (ReaSample) * (std::size_t) chunk * (std::size_t) numCh);
            ps->BufferDone (chunk);
        }
        const std::int64_t before = outLen();
        drain();
        if (outLen() == before) break;   // これ以上出ないなら終了
    }
    ps->FlushSamples();
    drain();
    delete ps;

    // 3) 実音のオンセット（頭）を自動検出して整列（先頭の遅延/無音を除去, 上限 maxLead）
    const std::int64_t avail = outLen();
    float peak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        for (float v : out[(std::size_t) ch]) peak = std::max (peak, std::abs (v));

    const float onsetThr = std::max (0.0005f, peak * 0.02f);
    std::int64_t onset = 0;
    for (std::int64_t i = 0, lim = std::min (avail, maxLead); i < lim; ++i)
    {
        float mx = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mx = std::max (mx, std::abs (out[(std::size_t) ch][(std::size_t) i]));
        if (mx > onsetThr) { onset = i; break; }
    }
    const std::int64_t len = std::max<std::int64_t> (0, std::min (expectedLen, avail - onset));

    // ★診断ログ（後で削除）
    {
        std::ofstream f ("C:/Users/biboo/otomad_reaper_dbg.txt", std::ios::app);
        f << "render semi=" << semi << " mode=" << mode << " sub=" << sub
          << " shift=" << shift << " onset=" << onset << " avail=" << avail
          << " expLen=" << expectedLen << " len=" << len << " n=" << n << " peak=" << peak << "\n";
    }

    auto sb = std::make_shared<SampleBuffer>();
    sb->numChannels        = numCh;
    sb->sampleRate         = sr;
    sb->originalSampleRate = sr;
    sb->name               = src->name + "_shift";
    sb->data.assign ((std::size_t) numCh, std::vector<float> ((std::size_t) len, 0.0f));
    for (int ch = 0; ch < numCh; ++ch)
        for (std::int64_t i = 0; i < len; ++i)
            sb->data[(std::size_t) ch][(std::size_t) i] = out[(std::size_t) ch][(std::size_t) (onset + i)];
    sb->numSamples = len;
    return sb;
}

} // namespace otomad
