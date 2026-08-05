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

namespace otomad
{

using ReaperGetPitchShiftAPI_t = IReaperPitchShift* (*) (int);

void PitchCache::configure (const SampleBuffer* src, int version, int mode, int sub, double sampleRate)
{
    std::lock_guard<std::mutex> lock (ownerLock);
    if (src == curSrc && version == curVersion && mode == curMode && sub == curSub
        && std::abs (sampleRate - curSr) < 1.0e-6)
        return;

    // 無効化: 新しい ready のみクリア。古いバッファは再生中の可能性があるので解放しない（graveyard保持）。
    for (auto& p : ready) p.store (nullptr, std::memory_order_release);
    reqLo.store (0); reqHi.store (0);

    curSrc = src; curVersion = version; curMode = mode; curSub = sub; curSr = sampleRate;
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

    int usedVersion = -1;
    if (auto buf = renderShift (semi, usedVersion))
    {
        std::lock_guard<std::mutex> lock (ownerLock);
        if (usedVersion == curVersion)   // レンダリング中に素材/モードが変わっていなければ公開
        {
            graveyard.push_back (buf);
            ready[(std::size_t) (semi - kMin)].store (buf.get(), std::memory_order_release);
        }
    }
    return true;
}

std::shared_ptr<SampleBuffer> PitchCache::renderShift (int semi, int& usedVersion)
{
    // 素材/モードのスナップショット
    const SampleBuffer* src; int mode, sub; double sr;
    {
        std::lock_guard<std::mutex> lock (ownerLock);
        src = curSrc; mode = curMode; sub = curSub; sr = curSr;
        usedVersion = curVersion;
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
    ps->set_tempo (1.0);                 // 長さ保持
    ps->SetQualityParameter ((mode << 16) + sub);
    ps->Reset();

    // 先頭のレイテンシ量を実測（無音を流す）→ 出力の頭をこの分だけ捨てて整列
    int latency = 0;
    {
        const int probe = 256;
        std::vector<double> tmp ((std::size_t) probe * (std::size_t) numCh, 0.0);
        int fed = 0;
        for (int g = 0; g < 4096; ++g)
        {
            if (ReaSample* b = ps->GetBuffer (probe))
            { std::memset (b, 0, sizeof (ReaSample) * (std::size_t) probe * (std::size_t) numCh); ps->BufferDone (probe); fed += probe; }
            if (ps->GetSamples (probe, tmp.data()) > 0) break;
            latency = fed;
            if (fed > 4 * (int) sr) break;
        }
        ps->Reset();
    }

    // 本レンダリング: 全入力を供給 → Flush → 全出力を回収
    const std::int64_t n = src->numSamples;
    std::vector<std::vector<float>> out ((std::size_t) numCh);
    for (auto& c : out) c.reserve ((std::size_t) (n + latency + 4096));

    const int chunk = 1024;
    std::vector<double> pull ((std::size_t) chunk * (std::size_t) numCh, 0.0);

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

    std::int64_t pos = 0;
    while (pos < n)
    {
        const int c = (int) std::min<std::int64_t> (chunk, n - pos);
        if (ReaSample* b = ps->GetBuffer (c))
        {
            for (int i = 0; i < c; ++i)
                for (int ch = 0; ch < numCh; ++ch)
                    b[(std::size_t) (i * numCh + ch)] = (ReaSample) src->sampleAtRaw (ch, pos + i);
            ps->BufferDone (c);
        }
        pos += c;
        drain();
    }
    ps->FlushSamples();
    drain();
    delete ps;

    // 頭の latency 分を捨てて素材と整列、長さは原音長に揃える
    auto sb = std::make_shared<SampleBuffer>();
    sb->numChannels       = numCh;
    sb->sampleRate        = sr;
    sb->originalSampleRate = sr;
    sb->name              = src->name + "_shift";
    sb->data.assign ((std::size_t) numCh, {});
    const std::size_t avail = out.empty() ? 0 : out[0].size();
    const std::size_t start = std::min ((std::size_t) latency, avail);
    const std::size_t len   = std::min ((std::size_t) n, avail - start);
    for (int ch = 0; ch < numCh; ++ch)
    {
        sb->data[(std::size_t) ch].assign ((std::size_t) n, 0.0f);
        for (std::size_t i = 0; i < len; ++i)
            sb->data[(std::size_t) ch][i] = out[(std::size_t) ch][start + i];
    }
    sb->numSamples = n;
    return sb;
}

} // namespace otomad
