#pragma once

#include <vector>

namespace otomad
{

//==============================================================================
/**
    エンジン間で共有する**読み取り専用**リソース（窓関数など）。DESIGN.md §2.1 / 規約9。
    可変状態（位相配列・FIFO・テンプレート）はここには置かない ─ それは各ボイスのエンジンが持つ。

    Phase 3 の WSOLA / Phase Vocoder はフレーム/FFTサイズ固定（48kHz基準の 2048/512）。
    パラメータ化（fftSize 2048/4096 等, §4.4）は後続で。
*/
struct EngineResources
{
    // WSOLA
    int wsolaFrame  = 2048;
    int wsolaHop    = 512;         // hopSynth
    int wsolaSearch = 480;         // ±10ms @48k 相当

    // Phase Vocoder
    int fftSize = 2048;
    int pvHop   = 512;             // hopSynth（COLA: Hann 75%）

    // Granular。WSOLA より短い粒にして粒立ちを出す。
    // granJitter: 粒の読み出し位置をランダムに揺らす量[サンプル]。0 にすると、
    // 粒どうしの位相差が規則的に並んで基音が打ち消し合うことがある（下の実測参照）。
    // 実測（440Hz正弦を -12..+12半音、ピッチ誤差の最悪値 / 振幅のばらつき）:
    //   1024 x2 jit128 →  15cent / 0.073   ← 採用（精度も滑らかさも最良の部類、粒も短い）
    //   2048 x2 jit 64 →   7cent / 0.107
    //   1024 x4 jit128 →  45cent / 0.275   重なりを増やすとジッタと相まって荒れる
    //   1024 x2 jit  0 → 218cent / 0.003   ジッタ無しは位相が規則的に並んで音程が狂う
    int granFrame  = 1024;
    int granHop    = 512;    // 重なり2倍（Hann は50%重ねで COLA を満たす）
    int granJitter = 128;

    std::vector<float> hannWsola;  // size = wsolaFrame
    std::vector<float> hannFft;    // size = fftSize
    std::vector<float> hannGran;   // size = granFrame

    void prepare (double sampleRate);
};

} // namespace otomad
