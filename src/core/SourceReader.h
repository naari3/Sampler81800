#pragma once

#include <cstdint>
#include "SampleBuffer.h"

namespace otomad
{

//==============================================================================
/**
    trim を吸収し、エンジンには「トリム開始点=位置0」の無限ストリームとして見せる。
    DESIGN.md §3.2 / 規約6。

    Phase 1 では trim + ゼロクロス吸着のみ。loop / reverse は Phase 5 で追加する。
    エンジンから見た位置は常にトリム開始点からの相対値。終端判定もトリム長で行う。
*/
class SourceReader
{
public:
    // startSample/endSample は buffer(data) 上の絶対インデックス。
    // snapZeroCross 有効時、start を ±2ms 内の立ち上がりゼロクロスへ吸着する（非RT時に一度だけ）。
    void configure (const SampleBuffer* buffer,
                    std::int64_t startSample,
                    std::int64_t endSample,
                    bool snapZeroCross) noexcept;

    int          getNumChannels()        const noexcept { return numCh; }
    std::int64_t getTrimmedLength()      const noexcept { return trimLen; }
    double       getTrimmedLengthSeconds() const noexcept { return sr > 0.0 ? (double) trimLen / sr : 0.0; }
    std::int64_t getTrimStart()          const noexcept { return start; }
    std::int64_t getTrimEnd()            const noexcept { return end; }

    // トリム相対の生サンプル。[0, trimLen) 外は 0。補間はエンジン側の責務 (§4.2)。
    float sampleAt (int ch, std::int64_t idx) const noexcept
    {
        if (buffer == nullptr || idx < 0 || idx >= trimLen)
            return 0.0f;
        return buffer->sampleAtRaw (ch, start + idx);
    }

    bool isFinished (double pos) const noexcept { return pos >= (double) trimLen; }

    // [pos-window, pos+window] で s[j-1]<=0 && s[j]>0 を満たす j のうち pos に最も近いものを返す。
    // 無ければ pos を返す（無限ループしない）。DESIGN.md §8.1。
    static std::int64_t snapToRisingZeroCross (const SampleBuffer& b, int ch,
                                               std::int64_t pos, std::int64_t window) noexcept;

private:
    const SampleBuffer* buffer  = nullptr;
    std::int64_t        start   = 0;
    std::int64_t        end     = 0;
    std::int64_t        trimLen = 0;
    int                 numCh   = 0;
    double              sr      = 0.0;
};

} // namespace otomad
