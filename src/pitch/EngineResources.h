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

    std::vector<float> hannWsola;  // size = wsolaFrame
    std::vector<float> hannFft;    // size = fftSize

    void prepare (double sampleRate);
};

} // namespace otomad
