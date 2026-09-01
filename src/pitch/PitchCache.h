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
#include "ElastiqueDirect.h"

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
    // ルートからの相対半音。octave(±4=±48半音) と pitchSemi(±48) を併用すると
    // ±48 では足りずクランプされ、その先は Varispeed が上乗せされて音色・長さが変わる。
    // ±96 まで持てばその併用でも素の再生が届く。
    static constexpr int kMin = -96, kMax = 96, kN = kMax - kMin + 1;
    // 要求ビットは範囲から語数を導く（範囲を広げたときに足りなくなるのを防ぐ）
    static constexpr int kWords = (kN + 63) / 64;

    void setApi (host::ReaperApi* a) noexcept { api = a; }
    // REAPER 外での代替バックエンド（実験機能）。null なら従来どおり REAPER 上でのみ動く。
    void setElastique (const ElastiqueDirect* e) noexcept { elastique = e; }

    // --- 音声スレッド ---
    const SampleBuffer* lookup (int semi) const noexcept
    {
        if (semi < kMin || semi > kMax) return nullptr;
        return ready[(std::size_t) (semi - kMin)].load (std::memory_order_acquire);
    }
    // 生成できない音程は最初から要求しない。
    // élastique 直読みは ±24 半音を超えると必ず空を返すので、要求すると
    //   「レンダ→空→ready にならない→また要求される」
    // を延々と繰り返し、進捗バーが上がらないまま出っぱなしになる（＝生成が止まって見える）。
    // 範囲外は要求せず、呼び出し側の Varispeed フォールバックに任せる（規約15）。
    void request (int semi) noexcept
    {
        if (semi < reqLo.load (std::memory_order_relaxed)
         || semi > reqHi.load (std::memory_order_relaxed)) return;
        if (semi < kMin || semi > kMax) return;
        const int bit = semi - kMin;
        req[(std::size_t) (bit >> 6)].fetch_or (1ull << (bit & 63), std::memory_order_release);
    }

    // いま実際に生成できる半音範囲（configure が更新する。UI/プリウォームの参照用）。
    void usableRange (int& lo, int& hi) const noexcept
    { lo = reqLo.load (std::memory_order_relaxed); hi = reqHi.load (std::memory_order_relaxed); }
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
    // elaMode: élastique 直読み時のアルゴリズム（0=Polyphonic, 1=Soloist）。
    // REAPER 経路では未使用だが、切り替えでキャッシュを作り直させるため変更検知に含める。
    bool configure (const SampleBuffer* src, int version, int mode, int sub,
                    double sampleRate, float formant, double timeRatio,
                    float start01, float end01, int elaMode);
    bool hasPending() const noexcept
    {
        for (auto& w : req) if (w.load() != 0) return true;
        return false;
    }

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
    {
        int n = 0;
        for (auto& w : req) n += std::popcount (w.load());
        return n;
    }

    // --- 背景スレッド ---
    // 保留中の半音を1つレンダリングして公開する。やることがあれば true。
    bool renderPending();

private:
    std::shared_ptr<SampleBuffer> renderShift (int semi, int& usedGen);

    host::ReaperApi*       api       = nullptr;
    const ElastiqueDirect* elastique = nullptr;

    // 生成可能な半音範囲。バックエンド（REAPER / élastique 直読み / 無し）で変わるので
    // configure（メッセージスレッド）が更新し、request からは atomic で読むだけにする。
    std::atomic<int> reqLo { kMin };
    std::atomic<int> reqHi { kMax };

    std::array<std::atomic<const SampleBuffer*>, (std::size_t) kN> ready {};
    std::array<std::atomic<std::uint64_t>, (std::size_t) kWords> req {};

    std::mutex ownerLock;                 // 背景/メッセージ用（音声スレッドは触らない）
    const SampleBuffer* curSrc = nullptr;
    int    curVersion = -1, curMode = 0, curSub = 0;
    double curSr = 48000.0;
    float  curFormant = 0.0f;      // 量子化済み（0.25半音刻み）
    double curTimeRatio = 1.0;     // 量子化済み（0.01刻み）
    float  curStart = 0.0f, curEnd = 1.0f;   // トリム範囲（量子化済み 0.001刻み）
    int    curElaMode = 0;                   // élastique 直読みのアルゴリズム
    int    curGen = 0;             // 設定が1つでも変わるたびに +1（レンダリング有効性の判定用）
    std::vector<std::shared_ptr<const SampleBuffer>> graveyard;   // 再生中バッファの寿命保持
};

} // namespace otomad
