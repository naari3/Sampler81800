#pragma once

#include <cstdint>
#include <vector>
#include "pitch/IPitchEngine.h"

namespace otomad
{

//==============================================================================
/**
    Granular（時間領域、長さ保持）。DESIGN.md §4.5。

    **1段構成**（WSOLA / Phase Vocoder の2段構成とは違う）:
      - ピッチ … 粒の *中* を pitchRatio でリサンプルして作る
      - 長さ   … 粒の *開始位置* の進み方 (hop * timeRatio) で作る
    この2つは別々の場所で効くので互いに干渉しない（規約5の趣旨）。

    > 規約5 の「解析hop = hopSynth * timeRatio / pitchRatio」は、
    > 「時間伸縮 → 出力段でリサンプル」の2段構成に対する式。本エンジンは出力段の
    > リサンプルを持たないので pitchRatio で割らない。詳しい導出は .cpp のコメント参照。

    初版は「WSOLA から相関探索を抜いただけ」の2段構成にしたが、位相を揃えずに OLA で
    時間伸縮すると粒の境界で位相が飛び、**基音そのものがズレた**（timeRatio=0.5 /
    pitchRatio=1 で 440Hz→407.5Hz）。WSOLA が探索を持っている理由がこれ。
    粒の中でリサンプルする形なら、粒内は元信号の忠実な移調なのでピッチは正確に出る。

    音の性格: 粒どうしの位相は揃えないので、境界のにじみ・ざらつきが残る。これが
    グラニュラーらしさで、狙って使うもの。相関探索が無いぶん WSOLA より軽い。

    プル型なので n==1 でも正しく動き、ブロック分割不変（規約7）。
    可変状態（acc / 出力リング / analysisPos）はこの実体＝ボイス固有（規約9）。
    共有するのは EngineResources の窓（読み取り専用）だけ。
*/
class GranularEngine : public IPitchEngine
{
public:
    void prepare (const PitchEngineContext&, EngineResources&) override;
    void reset() override;

    // 実測: ピッチ比 0.5 で 262、比 1.0 で -77、比 2.0 で -738。
    // frame(1024) は過大。最悪値を覆う hop 1つ分にする。
    int  getIntrinsicLatency() const override { return hop; }
    int  getTailSamples()      const override { return frame; }   // テールは流し切る長さ
    bool preservesDuration()   const override { return true; }

    void process (SourceReader& src, double& srcPos,
                  float* const* out, int numChannels, int n,
                  const float* pitchRatio, double timeRatio) override;

private:
    void synthesizeGrain (SourceReader& src, double pitchRatio, double timeRatio) noexcept;

    const float* hann = nullptr;
    int  frame = 1024, hop = 256;
    double jitter = 128.0;
    int  prepCh = 2;
    long cap = 4096;

    // 状態（ボイス固有）
    bool   needInit    = true;
    std::uint32_t rng  = 0;     // 粒ごとに進める決定的な擬似乱数（ブロック分割不変のため）
    double analysisPos = 0.0;   // 次の粒を読み始める入力位置
    long   outWrite    = 0;     // 出力リングへ書き込み済みのサンプル数
    long   outRead     = 0;     // 出力リングから読み出し済みのサンプル数

    std::vector<std::vector<float>> acc;      // [ch][frame] OLA分子
    std::vector<float>              accW;     // [frame]     OLA分母（窓和）
    std::vector<std::vector<float>> outRing;  // [ch][cap]   確定した出力
};

} // namespace otomad
