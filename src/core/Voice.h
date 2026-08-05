#pragma once

#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

#include "SampleBuffer.h"
#include "SourceReader.h"
#include "PortamentoGenerator.h"
#include "pitch/VarispeedEngine.h"

namespace otomad
{

//==============================================================================
/**
    ボイス。DESIGN.md §3.6。Phase 2 でポルタメント（半音ドメイン）とスチール時フェードを追加。
    ピッチは PortamentoGenerator が出す「ノート番号(float)」を基準に、offset/cents/bend/rootKey を
    足して比に変換する（規約4: 半音ドメイン補間）。
*/
class Voice
{
public:
    struct Params
    {
        float pitchSemi     = 0.0f;
        float pitchCents    = 0.0f;
        int   rootKey       = 60;
        float gainLin       = 1.0f;
        float pitchBendSemi = 0.0f;
        VarispeedEngine::Quality quality = VarispeedEngine::Quality::Hermite;
    };

    void prepare (double sampleRate, int maxBlock, int numChannels);
    void setParams (const Params& p) noexcept { params = p; }
    void setAdsr (float attackSec, float decaySec, float sustain, float releaseSec) noexcept;
    void setPortamentoConfig (PortamentoGenerator::Shape shape, float timeMs, float curve) noexcept;

    // 即時発音（フル・トリガ）。glide=true なら originNote から滑る。
    void noteOn (const SampleBuffer* sample, int midiNote, float velocity,
                 float sampleStart01, float sampleEnd01, bool snapZeroCross,
                 bool glide, float originNote);

    void glideTo (int midiNote) noexcept;              // mono レガート: ピッチのみ滑らす（sample/adsr維持）
    void setGlideOrigin (float originNote) noexcept;   // poly グループ内オリジン再割当 (§3.4)

    // 発音中のボイスを奪う: 5msフェードアウト後に予約ノートを発音（§3.5）。非アクティブなら即発音。
    void requestSteal (const SampleBuffer* sample, int midiNote, float velocity,
                       float sampleStart01, float sampleEnd01, bool snapZeroCross,
                       bool glide, float originNote);

    void noteOff() noexcept;
    void stop() noexcept;

    bool  isActive() const noexcept    { return active; }
    bool  isReleasing() const noexcept { return active && released; }
    int   getNote() const noexcept     { return midiNote; }
    float currentPitchNote() const noexcept { return porta.current(); }

    void render (float* const* out, int numChannels, int n) noexcept;

private:
    struct Pending
    {
        const SampleBuffer* sample = nullptr;
        int   note = 0; float vel = 0.0f;
        float s01 = 0.0f, e01 = 1.0f; bool snap = false;
        bool  glide = false; float originNote = 0.0f;
    };
    void startNote (const Pending& p) noexcept;

    bool   active = false;
    bool   released = false;
    int    midiNote = -1;
    float  velocity = 1.0f;
    double srcPos = 0.0;
    bool   sourceReleaseTriggered = false;
    double sampleRate = 44100.0;

    SourceReader        reader;
    VarispeedEngine     engine;
    juce::ADSR          adsr;
    juce::ADSR::Parameters adsrParams;
    PortamentoGenerator porta;
    Params              params;

    // スチール・フェード
    bool    stealing = false;
    float   stealGain = 1.0f;
    float   stealStep = 0.0f;
    Pending pending;

    std::vector<std::vector<float>> scratch;
    std::vector<float>              noteBuf;
    std::vector<float>              ratioBuf;
    std::vector<float*>             scratchPtrs;
    int preparedChannels = 0;
    int preparedBlock    = 0;
};

} // namespace otomad
