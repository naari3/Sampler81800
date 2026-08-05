#pragma once

#include <vector>
#include "pitch/IPitchEngine.h"

namespace otomad
{

//==============================================================================
/**
    WSOLA（時間領域、長さ保持）。DESIGN.md §4.3。
    2段構成: (1) 波形相似オーバーラップ加算によるタイムストレッチ → 中間ストリーム、
    (2) 中間ストリームを pitchRatio でリサンプル。

    プル型（中間ストリームを必要なだけ生成）なので n==1 でも正しく動き、ブロック分割不変（§8.1）。
    可変状態（acc / 中間リング / template / analysisPos）はこの実体＝ボイス固有（規約9）。
*/
class WsolaEngine : public IPitchEngine
{
public:
    void prepare (const PitchEngineContext&, EngineResources&) override;
    void reset() override;

    int  getIntrinsicLatency() const override { return frame; }
    int  getTailSamples()      const override { return frame; }
    bool preservesDuration()   const override { return true; }

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    void synthesizeFrame (SourceReader& src, double pitchRatio, double timeRatio) noexcept;
    float intAt (int ch, long idx) const noexcept;

    const EngineResources* res = nullptr;
    const float* hann = nullptr;
    int frame = 2048, hop = 512, search = 480, overlap = 1536;
    int prepCh = 2;
    long cap = 8192;

    // 状態
    bool   needInit = true;
    bool   firstFrame = true;
    double analysisPos = 0.0;
    double intReadPos  = 0.0;
    long   intWrite    = 0;

    std::vector<std::vector<float>> acc;      // [ch][frame]  OLA分子
    std::vector<float>              accW;      // [frame]      OLA分母（窓和）
    std::vector<std::vector<float>> intRing;   // [ch][cap]    中間ストリーム
    std::vector<float>              templateBuf; // [overlap]   相関テンプレート(ch0)
};

} // namespace otomad
