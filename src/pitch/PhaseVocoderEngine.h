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

    // 実測: ピッチ比 0.5 で 433、比 1.0 で 0、比 2.0 で -706。
    // N(2048) は過大（WsolaEngine.h の注記も参照）。最悪値を覆う hop 1つ分にする。
    int  getIntrinsicLatency() const override { return hop; }
    int  getTailSamples()      const override { return N; }   // テールは流し切る長さ
    bool preservesDuration()   const override { return true; }
    bool supportsFormant()     const override { return true; }
    void setFormantShift (float semitones) override { formantSemi = semitones; }
    void setPhaseLock (bool on) noexcept { phaseLock = on; }   // §4.4 identity phase locking

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    void synthesizeFrame (SourceReader& src, double pitchRatio, double timeRatio) noexcept;
    float intAt (int ch, long idx) const noexcept;

    const float* hann = nullptr;
    double sampleRate = 48000.0;   // フォルマント平滑幅を Hz で決めるのに要る
    int  N = 2048, hop = 512, nbins = 1025;
    int  prepCh = 2;
    long cap = 8192;
    float formantSemi = 0.0f;
    bool  phaseLock = true;

    bool   needInit = true, firstFrame = true;
    double analysisPos = 0.0, intReadPos = 0.0;
    long   intWrite = 0;

    std::vector<std::vector<float>> acc;
    std::vector<float>              accW;
    std::vector<std::vector<float>> intRing;
    std::vector<std::vector<float>> prevPhase, sumPhase;   // [ch][nbins]

    std::vector<std::complex<float>> spec;   // [N] スクラッチ（1chずつ）
    std::vector<float>               mag, phi, magShift, outPhase;    // [nbins]
    std::vector<float>               gain;    // [nbins] フォルマント用のゲイン曲線（平滑前）
    std::vector<int>                 peaks;   // ピーク bin リスト
};

} // namespace otomad
