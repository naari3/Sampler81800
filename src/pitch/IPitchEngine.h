#pragma once

#include "core/SourceReader.h"
#include "pitch/EngineResources.h"

namespace otomad
{

struct PitchEngineContext
{
    double sampleRate  = 44100.0;
    int    maxBlockSize = 512;
    int    numChannels  = 2;
};

//==============================================================================
/**
    ピッチシフトエンジンの共通インターフェース。DESIGN.md §4.1。

    - **実体はボイスごとに持つ**（規約9）。可変状態は実装が抱える。共有は読み取り専用 EngineResources のみ。
    - `srcPos` は呼び出し側(Voice)が保持し、エンジンが更新する（切替で再生位置が保たれる）。
    - `pitchRatio[i]` をサンプル精度で反映できるのは Varispeed のみ。フレーム系はフレーム先頭値で丸める。
*/
class IPitchEngine
{
public:
    virtual ~IPitchEngine() = default;

    virtual bool isAvailable() const { return true; }

    virtual void prepare (const PitchEngineContext&, EngineResources&) = 0;   // 非RT
    virtual void reset() = 0;                                                 // RTセーフ

    virtual int  getIntrinsicLatency() const = 0;
    virtual int  getTailSamples()      const = 0;   // 素材が尽きた後のエンジン内部残響長 (§3.6)
    virtual bool preservesDuration()   const = 0;
    virtual bool supportsFormant()     const { return false; }
    virtual void setFormantShift (float /*semitones*/) {}

    // Varispeed:  srcPos += pitchRatio[i]（timeRatio無視）
    // 長さ保持系: srcPos += timeRatio       （pitchRatio は周波数倍率に使用）
    virtual void process (SourceReader& src, double& srcPos,
                          float* const* out, int numChannels, int n,
                          const float* pitchRatio, double timeRatio) = 0;
};

} // namespace otomad
