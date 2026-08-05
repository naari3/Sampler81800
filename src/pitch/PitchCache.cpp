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

    // 先頭のレイテンシを**インパルス**で実測（頭に1発入れ、出力に現れる位置＝出力遅延）。
    // 無音方式だと Soloist 等のピッチ追従型で過大評価しアタックを削るため。応答が無ければ 0（頭を捨てない=安全側）。
    int latency = 0;
    {
        ps->Reset();
        const int probe = 256;
        std::vector<double> tmp ((std::size_t) probe * (std::size_t) numCh, 0.0);
        bool impulseFed = false;
        long outCount = 0;
        for (int g = 0; g < 8192; ++g)
        {
            if (ReaSample* b = ps->GetBuffer (probe))
            {
                std::memset (b, 0, sizeof (ReaSample) * (std::size_t) probe * (std::size_t) numCh);
                if (! impulseFed) { for (int ch = 0; ch < numCh; ++ch) b[(std::size_t) ch] = 1.0; impulseFed = true; }
                ps->BufferDone (probe);
            }
            const int got = ps->GetSamples (probe, tmp.data());
            int hit = -1;
            for (int i = 0; i < got; ++i)
            {
                double mx = 0.0;
                for (int ch = 0; ch < numCh; ++ch) mx = std::max (mx, std::abs (tmp[(std::size_t) (i * numCh + ch)]));
                if (mx > 0.0005) { hit = i; break; }
            }
            if (hit >= 0) { latency = (int) (outCount + hit); break; }
            outCount += got;
            if (outCount > 2 * (long) sr) break;
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

    // 頭の latency 分を捨てて素材と整列。長さは実際の出力長（ストレッチで変わる）。
    auto sb = std::make_shared<SampleBuffer>();
    sb->numChannels        = numCh;
    sb->sampleRate         = sr;
    sb->originalSampleRate = sr;
    sb->name               = src->name + "_shift";
    sb->data.assign ((std::size_t) numCh, {});
    const std::size_t avail = out.empty() ? 0 : out[0].size();
    const std::size_t start = std::min ((std::size_t) latency, avail);
    const std::size_t len   = avail - start;

    // ★診断ログ（後で削除）: 出力のピークも見る（無音データかどうかの判定）
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            for (float v : out[(std::size_t) ch]) peak = std::max (peak, std::abs (v));
        std::ofstream f ("C:/Users/biboo/otomad_reaper_dbg.txt", std::ios::app);
        f << "render semi=" << semi << " mode=" << mode << " sub=" << sub
          << " shift=" << shift << " lat=" << latency
          << " avail=" << avail << " len=" << len << " n=" << n
          << " peak=" << peak << "\n";
    }
    for (int ch = 0; ch < numCh; ++ch)
    {
        sb->data[(std::size_t) ch].assign (len, 0.0f);
        for (std::size_t i = 0; i < len; ++i)
            sb->data[(std::size_t) ch][i] = out[(std::size_t) ch][start + i];
    }
    sb->numSamples = (std::int64_t) len;
    return sb;
}

} // namespace otomad
