#pragma once

#include <cstdint>
#include <vector>

namespace otomad
{

//==============================================================================
/**
    区間内のピッチを検出して、その区間を**単一の音程に平坦化**する（Melodyne 的な処理）。
    DESIGN.md §3.11。

    狙い: 喋り声のように音程が揺れている素材を「一定の音程で歌う」状態にして、
    鍵盤を押したらその音程で鳴るようにする（音MAD の主用途）。

    方針:
    - フレームごとに YIN でピッチ曲線を採り、区間内の中央値を最寄りの半音へスナップした
      ものを目標にする。補正量は **半音(対数)ドメイン**で扱う（規約4）。
    - 無声区間・低信頼フレームは直前の補正量を保持する。0 に落とすと子音の前後で
      補正が飛んでワブるため。さらに時間方向にスムージングして検出ジッタを均す。
    - 実際のシフトは既存の長さ保持エンジン（Phase Vocoder）へ **サンプルごとの
      pitchRatio 配列**を渡して行う。timeRatio は 1.0 のままなので長さは変わらない。
      新しい DSP は書かない（規約5: pitchRatio と timeRatio を癒着させない）。

    **JUCE 非依存**。オフライン専用（RTスレッドから呼ばない: 確保もFFTも走る）。
*/

/** UI 表示用のピッチ曲線。 */
struct PitchContour
{
    double             hopSeconds = 0.0;   // フレーム間隔
    double             startSeconds = 0.0; // 先頭フレームの中心時刻
    std::vector<float> midi;               // 検出音程(MIDI実数)。0 は「検出できず」
};

struct FlattenResult
{
    bool         ok = false;
    double       detectedMidi = 0.0;   // 平坦化**前**の区間内中央値（MIDI実数）
    int          targetNote   = 60;    // スナップ先の半音
    // 平坦化**後**の実際の音程（MIDI実数）を出力から測り直したもの。
    // strength<1 では targetNote に届かないので、Root/Cent はこちらを基準に決めること。
    // targetNote を信じて Cent=0 にすると、DETECT の結果と食い違う（最大50cent）。
    double       resultMidi   = 0.0;
    int          voicedFrames = 0;     // 信頼できたフレーム数
    PitchContour contour;
    std::vector<std::vector<float>> audio;   // 平坦化後（in と同じ ch 数・長さ）
};

/** 解析パラメータ。既定値は 48kHz の喋り声を想定。 */
struct FlattenOptions
{
    // 解析窓長。yinWindow の検出下限 50Hz を satisfies するには W/2 >= sr/50、つまり
    // 40ms 以上が要る。実測でも 40ms が最良（30ms も同精度だが下限が 66Hz に上がり
    // 低い男声を取りこぼす）。
    double frameSeconds  = 0.040;
    double hopSeconds    = 0.010;   // 解析ホップ
    // 補正カーブの平滑化時定数。強すぎると補正そのものを鈍らせて「うねり」が残る。
    // 実測(残差 rms): 0ms→18.4ct / 10ms→15.5 / 15ms→14.1 / 20ms→14.5 / 40ms→30.4
    double smoothSeconds = 0.015;
    double maxAperiodicity = 0.25;  // これ以上は無声/非調和として捨てる
    double minRms          = 1.0e-3;// これ未満の窓は捨てる
    float  strength      = 1.0f;    // 0=無変化, 1=完全に平坦
    int    blockSize     = 512;     // オフラインレンダのブロック長
    // 補正カーブを何サンプルずらしてエンジンへ渡すか（群遅延の補償）。
    // 診断用に外から振れるようにしてある。負なら過去側へずらす。
    int    ratioOffsetSamples = 0;
};

/** 区間 [rangeStart, rangeEnd) を解析し、全体を1つの音程へ平坦化する。

    区間外のサンプルにも端の補正量を延長して適用するので、トリムを動かしても
    つなぎ目でピッチが飛ばない。失敗時は ok=false（音は変更しない）。

    @param in          planar [ch][sample]
    @param sampleRate  in のサンプルレート（原音SRで呼ぶこと。規約16）
*/
FlattenResult flattenToSinglePitch (const std::vector<std::vector<float>>& in,
                                    int numChannels, std::int64_t numSamples,
                                    double sampleRate,
                                    std::int64_t rangeStart, std::int64_t rangeEnd,
                                    const FlattenOptions& opt = {});

/** 解析だけ行う（UI のピッチ曲線プレビュー用）。音は生成しない。 */
FlattenResult analyseOnly (const std::vector<std::vector<float>>& in,
                           int numChannels, std::int64_t numSamples,
                           double sampleRate,
                           std::int64_t rangeStart, std::int64_t rangeEnd,
                           const FlattenOptions& opt = {});

} // namespace otomad
