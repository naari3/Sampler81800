#pragma once

#include <memory>
#include <vector>
#include "pitch/IPitchEngine.h"

namespace otomad
{

//==============================================================================
/**
    Stretch Library — 外部ライブラリ `signalsmith-stretch`（MIT・ヘッダオンリー）を使う。
    DESIGN.md §4.6。自作エンジン（WSOLA / Phase Vocoder / Granular）と聴き比べる基準にもなる。

    このライブラリは元々「入力 N サンプルを与えて出力 M サンプルを受け取る」API で、
    ピッチ（setTransposeFactor）と時間（N:M の比）が最初から独立している。
    そのため `timeRatio` をそのまま N/M として渡せる（規約5: 癒着させない）。

    実装上の注意:
    - `presetDefault()` は確保を伴うので **prepare でだけ** 呼ぶ（規約1）。
      さらに process 内の一時バッファも初回だけ確保するため、prepare で無音を通して
      容量を作りきっておく（ウォームアップ）。
    - 乱数シードは固定する。既定コンストラクタは `std::random_device` を引くので、
      同じ入力でも実行ごとに出力が変わってしまい、ブロック分割不変性を検証できない。
    - ヘッダをこのファイルに持ち込まない（テンプレートが重く、コンパイル時間に効く）ので
      実体は pimpl で隠す。
*/
class StretchLibEngine : public IPitchEngine
{
public:
    StretchLibEngine();
    ~StretchLibEngine() override;

    void prepare (const PitchEngineContext&, EngineResources&) override;
    void reset() override;

    int  getIntrinsicLatency() const override { return latency; }
    int  getTailSamples()      const override { return latency; }
    bool preservesDuration()   const override { return true; }

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    int    latency  = 0;
    int    prepCh   = 2;
    int    maxBlock = 512;
    double srcRead  = 0.0;    // 入力の読み出し位置（小数）。srcPos と一致させる
    bool   needInit = true;

    std::vector<std::vector<float>> inBuf;   // [ch][cap] 入力スクラッチ
    std::vector<float>              dumpBuf; // 使わないchの出力の捨て先（入力と別領域にする）
    std::vector<const float*>       inPtr;
    std::vector<float*>             outPtr;
    int cap = 0;
};

} // namespace otomad
