#pragma once

#include <vector>
#include "pitch/IPitchEngine.h"

namespace otomad
{
namespace host { class ReaperApi; }

//==============================================================================
/**
    REAPER の内蔵ピッチシフタ（élastique 等）を IReaperPitchShift 経由で利用する。DESIGN.md §5.4。
    **REAPER上でのみ実動作**。非REAPERでは isAvailable()==false となり、Voice が代替エンジンへ
    フォールバックする（規約2）。SDK 型（windows.h 依存）は .cpp に隔離する。

    ★ in-REAPER の実レンダリングは実機検証が必要（この環境では起動不可）。
*/
class ReaperPitchShiftEngine : public IPitchEngine
{
public:
    ~ReaperPitchShiftEngine() override;

    void setReaperApi (host::ReaperApi* a) noexcept { api = a; }
    void setSubMode (int m) noexcept { subMode = m; }

    void prepare (const PitchEngineContext&, EngineResources&) override;
    void reset() override;

    bool isAvailable() const override { return pitchShift != nullptr; }
    int  getIntrinsicLatency() const override { return latency; }
    int  getTailSamples()      const override { return latency > 0 ? latency : 2048; }
    bool preservesDuration()   const override { return true; }

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    void destroyShifter() noexcept;

    host::ReaperApi* api = nullptr;
    void*  pitchShift = nullptr;   // 不透明 reaper::IReaperPitchShift*
    int    subMode = 0;
    int    latency = 0;
    double sampleRate = 48000.0;
    int    numCh = 2;

    std::vector<double> pullScratch;   // GetSamples 出力（interleaved double）
};

} // namespace otomad
