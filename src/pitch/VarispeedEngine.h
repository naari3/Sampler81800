#pragma once

#include "pitch/IPitchEngine.h"

namespace otomad
{

//==============================================================================
/**
    Varispeed（リサンプリング）。DESIGN.md §4.2。長さはピッチに従属（長さ保持しない）。
    pitchRatio[i] をサンプル精度で反映できる唯一のエンジン。Phase 1: Linear/Hermite。
*/
class VarispeedEngine : public IPitchEngine
{
public:
    enum class Quality { Linear, Hermite };
    void    setQuality (Quality q) noexcept { quality = q; }
    Quality getQuality () const noexcept    { return quality; }

    void prepare (const PitchEngineContext&, EngineResources&) override {}
    void reset() override {}
    int  getIntrinsicLatency() const override { return 0; }
    int  getTailSamples()      const override { return 0; }
    bool preservesDuration()   const override { return false; }

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    Quality quality = Quality::Hermite;
};

} // namespace otomad
