#pragma once

#include <cmath>

namespace otomad::pitchdetect
{

//==============================================================================
/**
    YIN による基本周波数推定。DESIGN.md §3.1。

    もとは PluginProcessor.cpp 内の static 関数だったが、DETECT ROOT と
    ピッチ平坦化 (PitchFlattener) の両方から使うのでコアへ切り出した。
    **JUCE 非依存**なので単体テストできる。
*/

/** 1窓の YIN。

    @param x       DC除去済みのモノ窓
    @param W       窓長
    @param sr      サンプルレート
    @param outFreq 基本周波数[Hz]（検出できなければ 0）
    @returns 非周期性 (0=完全周期, 小さいほど信頼できる)。0.2 未満なら実用上信頼してよい。
*/
double yinWindow (const float* x, int W, double sr, double& outFreq) noexcept;

/** Hz → MIDI ノート番号（実数）。A4=69=440Hz。 */
inline double hzToMidi (double hz) noexcept
{
    return 69.0 + 12.0 * std::log2 (hz / 440.0);
}

/** MIDI ノート番号（実数）→ Hz。 */
inline double midiToHz (double midi) noexcept
{
    return 440.0 * std::exp2 ((midi - 69.0) / 12.0);
}

} // namespace otomad::pitchdetect
