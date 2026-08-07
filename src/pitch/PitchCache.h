#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "core/SampleBuffer.h"

namespace otomad
{
namespace host { class ReaperApi; }

//==============================================================================
/**
    ピッチシフト・キャッシュ。DESIGN.md の発想（オフライン élastique を利用）。

    サンプラーは同じ素材を各音程で鳴らすので、**整数半音ごとに一度だけ REAPER の
    ピッチシフタでオフラインレンダリングして保持**し、再生は「ピッチ済みバッファを
    遅延ゼロで読む」だけにする。リアルタイムに élastique を通す必要がなくなり、
    レイテンシ・ぷつぷつ・CPU の問題を回避できる。

    スレッド規約:
    - 音声スレッド : lookup()/request() のみ（atomicのみ、ロック無し）。
    - 背景スレッド : renderPending()（実レンダリング）。
    - メッセージ  : configure()（素材/モード変更の反映）。
*/
class PitchCache
{
public:
    static constexpr int kMin = -48, kMax = 48, kN = kMax - kMin + 1;

    void setApi (host::ReaperApi* a) noexcept { api = a; }

    // --- 音声スレッド ---
    const SampleBuffer* lookup (int semi) const noexcept
    {
        if (semi < kMin || semi > kMax) return nullptr;
        return ready[(std::size_t) (semi - kMin)].load (std::memory_order_acquire);
    }
    void request (int semi) noexcept
    {
        if (semi < kMin || semi > kMax) return;
        const int bit = semi - kMin;
        (bit < 64 ? reqLo : reqHi).fetch_or (1ull << (bit & 63), std::memory_order_release);
    }
    // プリウォーム: 未生成の音程域をまとめてリクエスト（停止中に背景で貯める）。
    void requestRange (int lo, int hi) noexcept
    {
        for (int s = std::max (lo, kMin); s <= std::min (hi, kMax); ++s)
            if (ready[(std::size_t) (s - kMin)].load (std::memory_order_acquire) == nullptr)
                request (s);
    }

    // --- メッセージスレッド ---
    // 素材/モード/フォルマント/ストレッチ(timeRatio) が変わっていたら ready を無効化して作り直す。
    // （古いバッファは再生中の可能性があるので解放しない=graveyard保持）
    // 設定が変わっていたら true（プリウォームの再要求に使う）。
    // start01/end01: トリム範囲（正規化 0..1）。この範囲だけをレンダするので、トリム変更でも作り直す。
    bool configure (const SampleBuffer* src, int version, int mode, int sub,
                    double sampleRate, float formant, double timeRatio,
                    float start01, float end01);
    bool hasPending() const noexcept
    { return reqLo.load() != 0 || reqHi.load() != 0; }

    // --- 進捗（UI用・任意スレッドから読み取り可, atomicのみ） ---
    // 生成済み半音数（ready 非null の数）。
    int readyCount() const noexcept
    {
        int n = 0;
        for (auto& p : ready) if (p.load (std::memory_order_acquire) != nullptr) ++n;
        return n;
    }
    // 残り生成待ち半音数（リクエストビットの立っている数）。
    int pendingCount() const noexcept
    { return std::popcount (reqLo.load()) + std::popcount (reqHi.load()); }

    // --- 背景スレッド ---
    // 保留中の半音を1つレンダリングして公開する。やることがあれば true。
    bool renderPending();

private:
    std::shared_ptr<SampleBuffer> renderShift (int semi, int& usedGen);

    host::ReaperApi* api = nullptr;

    std::array<std::atomic<const SampleBuffer*>, (std::size_t) kN> ready {};
    std::atomic<std::uint64_t> reqLo { 0 }, reqHi { 0 };

    std::mutex ownerLock;                 // 背景/メッセージ用（音声スレッドは触らない）
    const SampleBuffer* curSrc = nullptr;
    int    curVersion = -1, curMode = 0, curSub = 0;
    double curSr = 48000.0;
    float  curFormant = 0.0f;      // 量子化済み（0.25半音刻み）
    double curTimeRatio = 1.0;     // 量子化済み（0.01刻み）
    float  curStart = 0.0f, curEnd = 1.0f;   // トリム範囲（量子化済み 0.001刻み）
    int    curGen = 0;             // 設定が1つでも変わるたびに +1（レンダリング有効性の判定用）
    std::vector<std::shared_ptr<const SampleBuffer>> graveyard;   // 再生中バッファの寿命保持
};

} // namespace otomad
