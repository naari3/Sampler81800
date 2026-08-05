#pragma once

#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

#include "SampleBuffer.h"
#include "SourceReader.h"
#include "pitch/VarispeedEngine.h"

namespace otomad
{

//==============================================================================
/**
    Phase 1 の単ボイス（モノフォニック）。DESIGN.md §3.6 の簡易版。
    Phase 2 で VoiceManager + ポリ + ポルタメントに拡張、Phase 3 で IPitchEngine 化する。
*/
class Voice
{
public:
    struct Params
    {
        float pitchSemi  = 0.0f;
        float pitchCents = 0.0f;
        int   rootKey    = 60;
        float gainLin    = 1.0f;
        VarispeedEngine::Quality quality = VarispeedEngine::Quality::Hermite;
    };

    void prepare (double sampleRate, int maxBlock, int numChannels);
    void setParams (const Params& p) noexcept { params = p; }
    void setAdsr (float attackSec, float decaySec, float sustain, float releaseSec) noexcept;

    // sample の寿命は呼び出し側（Processor の graveyard）が保証する。
    void noteOn (const SampleBuffer* sample, int midiNote, float velocity,
                 float sampleStart01, float sampleEnd01, bool snapZeroCross);
    void noteOff() noexcept;
    void stop() noexcept;                 // ハード停止（サンプル差し替え / all sound off）

    bool isActive() const noexcept { return active; }
    int  getNote() const noexcept  { return midiNote; }

    // out[ch] へ加算する（startSample オフセット済みポインタを渡すこと）。
    void render (float* const* out, int numChannels, int n) noexcept;

private:
    bool   active = false;
    int    midiNote = -1;
    float  velocity = 1.0f;
    double srcPos = 0.0;
    bool   sourceReleaseTriggered = false;
    double sampleRate = 44100.0;

    SourceReader    reader;
    VarispeedEngine engine;
    juce::ADSR      adsr;
    juce::ADSR::Parameters adsrParams;
    Params          params;

    std::vector<std::vector<float>> scratch;   // 補間出力 [ch][n]
    std::vector<float>              ratioBuf;
    std::vector<float*>             scratchPtrs;
    int  preparedChannels = 0;
    int  preparedBlock    = 0;
};

} // namespace otomad
