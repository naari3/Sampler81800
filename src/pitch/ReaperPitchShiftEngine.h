#pragma once

#include <string>
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
    void setMode (int m) noexcept    { modeSel = m; }
    void setSubMode (int s) noexcept { subSel = s; }
    void reconfigure();   // 現在の mode/sub を適用＋レイテンシ再実測（非RT・suspend下で呼ぶ）

    // GUI表示用（メッセージスレッドからのみ読む）
    int         getModeCount() const noexcept    { return numModes; }
    int         getSubModeCount() const noexcept { return numSubs; }
    const std::string& getModeName() const noexcept    { return modeName; }
    const std::string& getSubModeName() const noexcept { return subName; }

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
    void applyMode();   // (mode<<16)+sub を SetQualityParameter + レイテンシ実測 + 名前解決

    host::ReaperApi* api = nullptr;
    void*  pitchShift = nullptr;   // 不透明 reaper::IReaperPitchShift*
    int    modeSel = 0, subSel = 0;
    int    numModes = 0, numSubs = 0;   // 総モード数 / 現在モードのサブモード数
    std::string modeName, subName;
    int    latency = 0;
    double sampleRate = 48000.0;
    int    maxBlock = 512;
    int    numCh = 2;
    double lastShift = -1.0;
    double lastTempo = -1.0;

    std::vector<double> pullScratch;     // GetSamples 出力（interleaved double）

    // 出力FIFO: élastique のフレーム粒度でブロック内の過不足が出るのを吸収（ぷつぷつ防止）
    std::vector<double> fifo;
    int fifoCap = 0, fifoRead = 0, fifoWrite = 0, fifoCount = 0;
};

} // namespace otomad
