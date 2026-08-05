#pragma once

#include <complex>
#include <vector>
#include "pitch/IPitchEngine.h"

namespace otomad
{

//==============================================================================
/**
    Phase Vocoder（周波数領域、長さ保持）。DESIGN.md §4.4。
    stage1: フレーム毎に FFT→位相を再構成してタイムストレッチ（中間ストリーム生成）、
    stage2: 中間ストリームを pitchRatio でリサンプル。

    COLA は合成側 hop 固定で満たし、解析 hop = hopSynth*timeRatio/pitchRatio を可変にする（§4.4）。
    プル型でブロック分割不変。位相状態はボイス固有（規約9）。
*/
class PhaseVocoderEngine : public IPitchEngine
{
public:
    void prepare (const PitchEngineContext&, EngineResources&) override;
    void reset() override;

    int  getIntrinsicLatency() const override { return N; }
    int  getTailSamples()      const override { return N; }
    bool preservesDuration()   const override { return true; }
    bool supportsFormant()     const override { return true; }
    void setFormantShift (float semitones) override { formantSemi = semitones; }

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    void synthesizeFrame (SourceReader& src, double pitchRatio, double timeRatio) noexcept;
    float intAt (int ch, long idx) const noexcept;

    const float* hann = nullptr;
    int  N = 2048, hop = 512, nbins = 1025;
    int  prepCh = 2;
    long cap = 8192;
    float formantSemi = 0.0f;

    bool   needInit = true, firstFrame = true;
    double analysisPos = 0.0, intReadPos = 0.0;
    long   intWrite = 0;

    std::vector<std::vector<float>> acc;
    std::vector<float>              accW;
    std::vector<std::vector<float>> intRing;
    std::vector<std::vector<float>> prevPhase, sumPhase;   // [ch][nbins]

    std::vector<std::complex<float>> spec;   // [N] スクラッチ（1chずつ）
    std::vector<float>               mag, phi, magShift;    // [nbins]
};

} // namespace otomad
