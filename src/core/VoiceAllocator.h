#pragma once

#include <cstdint>
#include <vector>

namespace otomad
{

//==============================================================================
/**
    ポリのボイス割り当て判定（JUCE非依存・純ロジック）。DESIGN.md §3.5。

    方針: 同一ノートの再発音を最優先で拾い（連打での積み上がり防止, (d)）、
    次に空きボイス、どちらも無ければ**最も古いボイスを奪う**。
*/
struct VoiceSlotState
{
    bool         active   = false;
    int          note     = -1;
    std::uint64_t onTime  = 0;   // noteOn 時刻（サンプル単位の単調増加値）
    bool         releasing = false;
};

// 使うべきスロットのインデックスを返す。states は空でない前提。
inline int chooseVoiceSlot (const std::vector<VoiceSlotState>& states, int note)
{
    const int n = (int) states.size();

    // 1) 同一ノートで発音中のボイス
    for (int i = 0; i < n; ++i)
        if (states[(std::size_t) i].active && states[(std::size_t) i].note == note)
            return i;

    // 2) 空き（非アクティブ）ボイス
    for (int i = 0; i < n; ++i)
        if (! states[(std::size_t) i].active)
            return i;

    // 3) 全部埋まっていれば、リリース中を優先し、なければ最も古いものを奪う
    int bestReleasing = -1; std::uint64_t bestRelTime = UINT64_MAX;
    int oldest = 0;         std::uint64_t oldestTime  = UINT64_MAX;
    for (int i = 0; i < n; ++i)
    {
        const auto& s = states[(std::size_t) i];
        if (s.releasing && s.onTime < bestRelTime) { bestRelTime = s.onTime; bestReleasing = i; }
        if (s.onTime < oldestTime)                 { oldestTime  = s.onTime; oldest = i; }
    }
    return bestReleasing >= 0 ? bestReleasing : oldest;
}

} // namespace otomad
