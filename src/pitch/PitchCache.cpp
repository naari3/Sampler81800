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

bool PitchCache::configure (const SampleBuffer* src, int version, int mode, int sub,
                            double sampleRate, float formant, double timeRatio,
                            float start01, float end01)
{
    // 微小変化での再レンダリング連発を避けるため量子化
    const float  fq = std::round (formant * 4.0f) / 4.0f;        // 0.25半音刻み
    const double tq = std::round (timeRatio * 100.0) / 100.0;    // 0.01刻み
    const float  sq = std::round (std::clamp (start01, 0.0f, 1.0f) * 1000.0f) / 1000.0f;   // 0.1%刻み
    const float  eq = std::round (std::clamp (end01,   0.0f, 1.0f) * 1000.0f) / 1000.0f;

    std::lock_guard<std::mutex> lock (ownerLock);
    if (src == curSrc && version == curVersion && mode == curMode && sub == curSub
        && std::abs (sampleRate - curSr) < 1.0e-6
        && std::abs (fq - curFormant) < 1.0e-6 && std::abs (tq - curTimeRatio) < 1.0e-6
        && std::abs (sq - curStart) < 1.0e-6 && std::abs (eq - curEnd) < 1.0e-6)
        return false;

    // 無効化: 新しい ready のみクリア。古いバッファは再生中の可能性があるので解放しない（graveyard保持）。
    for (auto& p : ready) p.store (nullptr, std::memory_order_release);
    reqLo.store (0); reqHi.store (0);

    curSrc = src; curVersion = version; curMode = mode; curSub = sub; curSr = sampleRate;
    curFormant = fq; curTimeRatio = tq; curStart = sq; curEnd = eq;
    ++curGen;   // 設定が変わった → 進行中のレンダリングは無効化される
    return true;
}

bool PitchCache::renderPending()
{
    int semi = 0; bool found = false;
    {
        // 保留ビットを1つ「原子的に」取り出す（複数背景スレッドが同じ半音を二重処理しないように、
        // fetch_and の戻り値で自分がクリアした場合だけ確定する）。
        for (int b = 0; b < 64 && ! found; ++b)
        {
            const std::uint64_t mask = 1ull << b;
            if (reqLo.load() & mask)
            {
                if (reqLo.fetch_and (~mask) & mask) { semi = kMin + b; found = true; }
            }
        }
        for (int b = 0; b < (kN - 64) && ! found; ++b)
        {
            const std::uint64_t mask = 1ull << b;
            if (reqHi.load() & mask)
            {
                if (reqHi.fetch_and (~mask) & mask) { semi = kMin + 64 + b; found = true; }
            }
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
    const SampleBuffer* src; int mode, sub; double sr; float formant; double timeRatio; float start, end;
    {
        std::lock_guard<std::mutex> lock (ownerLock);
        src = curSrc; mode = curMode; sub = curSub; sr = curSr;
        formant = curFormant; timeRatio = curTimeRatio;
        start = curStart; end = curEnd;
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

    // トリム範囲 [base, base+n) だけをレンダする（Start/End で指定した範囲のみキャッシュ）。
    const std::int64_t total = src->numSamples;
    std::int64_t base   = (std::int64_t) std::llround ((double) start * (double) total);
    std::int64_t endIdx = (std::int64_t) std::llround ((double) end   * (double) total);
    base   = std::clamp<std::int64_t> (base,   0, total);
    endIdx = std::clamp<std::int64_t> (endIdx, 0, total);
    if (endIdx <= base)
        return nullptr;   // 空トリム

    // 入力(トリム範囲)ピーク（メイクアップゲイン算出用）
    float srcPeak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        for (std::int64_t i = base; i < endIdx; ++i)
            srcPeak = std::max (srcPeak, std::abs ((float) src->sampleAtRaw (ch, i)));

    ps->set_srate (sr);
    ps->set_nch (numCh);
    ps->set_shift (shift);
    // フォルマント: set_formant_shift(0.0) は「フォルマント保持モードで0シフト」を起動し、
    // élastique の出力をほぼ無音にする（実測: outPeak が 1/100 に低下）。API仕様では shift<0 は
    // 「保持モード時のみ適用（＝通常のピッチシフト・フォルマント非操作）」なので、シフト無し(0)や
    // 下方向は負値で渡して実質オフ＝フル出力にする。正の値のときだけ明示的にフォルマントを上げる。
    ps->set_formant_shift (formant > 0.05f ? (double) formant : -1.0);
    ps->set_tempo (timeRatio > 0.0 ? timeRatio : 1.0);   // ストレッチ(長さ)を焼き込む
    ps->SetQualityParameter ((mode << 16) + sub);
    ps->Reset();

    // --- オフライン・レンダリング（プローブ非依存の堅牢版）---
    const std::int64_t n = endIdx - base;   // トリム範囲の長さ
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

    // 1) 実入力を供給（トリム範囲のみ）
    for (std::int64_t pos = 0; pos < n; pos += chunk)
    {
        const int c = (int) std::min<std::int64_t> (chunk, n - pos);
        if (ReaSample* b = ps->GetBuffer (c))
        {
            for (int i = 0; i < c; ++i)
                for (int ch = 0; ch < numCh; ++ch)
                    b[(std::size_t) (i * numCh + ch)] = (ReaSample) src->sampleAtRaw (ch, base + pos + i);
            ps->BufferDone (c);
        }
        drain();
    }

    // 2) 内部に溜まった本体を無音で押し出す（大レイテンシモード対策）。十分な長さになるまで、
    //    または出力がこれ以上増えなくなるまで。
    const std::int64_t maxLead = (std::int64_t) (0.3 * sr);            // 先頭で除去する上限
    // 内部に溜まった本体を無音で押し出す。実音が揃う分（expLen＋先頭遅延＋余白）まで出れば十分なので
    // そこで止める（高速化）。tempo=1 だと供給した無音がそのまま 1:1 で出力に混ざるため、target で
    // 打ち切らないと無駄に大量の無音を処理してしまう。target・出力停止・上限のいずれかで終了。
    const std::int64_t target     = expectedLen + maxLead + (std::int64_t) (0.25 * sr);
    const std::int64_t maxSilence = std::max (expectedLen, n) + 2 * (std::int64_t) sr;  // 上限（保険）
    std::int64_t silenceFed = 0;
    int emptyStreak = 0;
    while (silenceFed < maxSilence && outLen() < target)
    {
        if (ReaSample* b = ps->GetBuffer (chunk))
        {
            std::memset (b, 0, sizeof (ReaSample) * (std::size_t) chunk * (std::size_t) numCh);
            ps->BufferDone (chunk);
        }
        silenceFed += chunk;
        const std::int64_t before = outLen();
        drain();
        if (outLen() == before) { if (++emptyStreak >= 16) break; }   // 持続的に無出力なら完了
        else emptyStreak = 0;
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

    // メイクアップゲイン: élastique 等は時間圧縮/ピッチシフト時に全体レベルを落とすことがある
    // （特にノイズ質感の素材を強く圧縮するとグレインの位相が揃わず peak が下がる）。
    // 出力ピークを入力ピークまで持ち上げてレベルを保つ。増幅のみ（減衰はしない）。
    // ほぼ無音の取りこぼしを過剰ブーストしないよう上限を設ける。
    float makeup = 1.0f;
    if (peak > 1.0e-4f && srcPeak > 1.0e-4f)
        makeup = std::clamp (srcPeak / peak, 1.0f, 8.0f);

    auto sb = std::make_shared<SampleBuffer>();
    sb->numChannels        = numCh;
    sb->sampleRate         = sr;
    sb->originalSampleRate = sr;
    sb->name               = src->name + "_shift";
    sb->data.assign ((std::size_t) numCh, std::vector<float> ((std::size_t) len, 0.0f));
    for (int ch = 0; ch < numCh; ++ch)
        for (std::int64_t i = 0; i < len; ++i)
            sb->data[(std::size_t) ch][(std::size_t) i] = makeup * out[(std::size_t) ch][(std::size_t) (onset + i)];
    sb->numSamples = len;
    return sb;
}

} // namespace otomad
