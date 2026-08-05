#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace otomad
{

//==============================================================================
/**
    読み込んだサンプル素材。DESIGN.md §3.1。

    - `data`     : 再生用。常にホストSRへ変換済み（planar）。RTスレッドは read only。
    - `original` : 原音（native SR）。保存・SR再変換の元。捨てない (§3.1 / 規約16)。

    設計書は juce::AudioBuffer を挙げているが、ここでは DSPコアを JUCE 非依存にして
    単体テスト可能にするため planar な std::vector で保持する（読み出しは連続領域で RT安全）。
*/
struct SampleBuffer
{
    std::vector<std::vector<float>> data;         // host SR, planar [ch][sample]
    int          numChannels = 0;
    std::int64_t numSamples  = 0;
    double       sampleRate  = 0.0;               // = host SR

    std::vector<std::vector<float>> original;     // native SR, planar
    double       originalSampleRate = 0.0;

    std::string  name;
    std::string  path;                            // 元ファイルのフルパス（保存/再読込用, §3.9）
    std::vector<std::pair<float, float>> peaks;   // (min,max) per bucket, モノミックス

    // 範囲外は 0。再生バッファ(data)への生アクセス。
    float sampleAtRaw (int ch, std::int64_t idx) const noexcept
    {
        if (ch < 0 || ch >= numChannels)        return 0.0f;
        if (idx < 0 || idx >= numSamples)       return 0.0f;
        return data[(std::size_t) ch][(std::size_t) idx];
    }
};

} // namespace otomad
