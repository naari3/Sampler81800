#pragma once

#include <cstdint>
#include "core/SourceReader.h"

namespace otomad
{

//==============================================================================
/**
    Varispeed（リサンプリング）。DESIGN.md §4.2。
    「テープを速く回す」方式で、長さはピッチに従属する（長さ保持しない）。

    Phase 1 では Linear / Hermite の2品質。Sinc は Phase 5。
    Phase 3 で IPitchEngine 抽象に載せ替え、timeRatio を受ける形に確定させる。
    現段階では pitchRatio だけを受ける（Varispeed は timeRatio を構造上使わない）。
*/
class VarispeedEngine
{
public:
    enum class Quality { Linear, Hermite };

    void    setQuality (Quality q) noexcept { quality = q; }
    Quality getQuality () const noexcept    { return quality; }

    // out[ch] に長さ n を書き込む（上書き）。srcPos は呼び出し側が保持し、ここで更新する。
    // pitchRatio[i] : 出力サンプル i におけるピッチ比 (2^(st/12))。Varispeed はこれをサンプル精度で反映できる。
    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio) noexcept;

private:
    Quality quality = Quality::Hermite;
};

} // namespace otomad
