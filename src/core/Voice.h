#pragma once

#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

#include "SampleBuffer.h"
#include "SourceReader.h"
#include "PortamentoGenerator.h"
#include "pitch/IPitchEngine.h"
#include "pitch/VarispeedEngine.h"
#include "pitch/WsolaEngine.h"
#include "pitch/PhaseVocoderEngine.h"
#include "pitch/ReaperPitchShiftEngine.h"

namespace otomad
{
namespace host { class ReaperApi; }


// 固定レイテンシ (§5.5)。WSOLA/PV の intrinsic (=frame/fft=2048) を覆う定数。
inline constexpr int kFixedLatency = 2048;

//==============================================================================
/**
    ボイス。DESIGN.md §3.6。Phase 3 で **エンジンをボイス所有**（規約9）、timeRatio(durationMode)、
    固定レイテンシ整列、テールドレイン（getTailSamples + 整列分）を確定。
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

    struct EngineControl
    {
        int   algorithm   = 0;    // 0 Varispeed / 1 WSOLA / 2 PhaseVocoder /（3-5は未実装→PVへ）
        int   durationMode = 0;   // 0 Natural / 1 Sync / 2 Manual
        float stretchAmount = 1.0f;
        float syncBeats     = 1.0f;
        double hostBpm      = 120.0;
        bool  hostBpmValid  = false;
        float formantSemi   = 0.0f;
        bool  phaseLock     = true;
        int   reaperSubMode = 0;
    };

    void prepare (double sampleRate, int maxBlock, int numChannels,
                  EngineResources& resources, host::ReaperApi* reaperApi);
    void setParams (const Params& p) noexcept { params = p; }
    void setAdsr (float attackSec, float decaySec, float sustain, float releaseSec) noexcept;
    void setPortamentoConfig (PortamentoGenerator::Shape shape, float timeMs, float curve) noexcept;
    void setEngineControl (const EngineControl& c) noexcept;   // fallback判定込み
    bool isFallbackActive() const noexcept { return fallbackActive; }

    void noteOn (const SampleBuffer* sample, int midiNote, float velocity,
                 float sampleStart01, float sampleEnd01, bool snapZeroCross,
                 bool glide, float originNote);
    void glideTo (int midiNote) noexcept;
    void setGlideOrigin (float originNote) noexcept;
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
        int note = 0; float vel = 0.0f;
        float s01 = 0.0f, e01 = 1.0f; bool snap = false;
        bool glide = false; float originNote = 0.0f;
    };
    void startNote (const Pending& p) noexcept;
    double resolveTimeRatio() noexcept;
    IPitchEngine* pickEngine (int algorithm) noexcept;

    bool   active = false;
    bool   released = false;
    int    midiNote = -1;
    float  velocity = 1.0f;
    double srcPos = 0.0;
    bool   sourceReleaseTriggered = false;
    long   drainCounter = 0;
    double sampleRate = 44100.0;

    SourceReader        reader;
    VarispeedEngine        varispeed;
    WsolaEngine            wsola;
    PhaseVocoderEngine     phaseVocoder;
    ReaperPitchShiftEngine reaper;
    IPitchEngine*          activeEngine = &varispeed;
    bool                   fallbackActive = false;

    juce::ADSR          adsr;
    juce::ADSR::Parameters adsrParams;
    PortamentoGenerator porta;
    Params              params;
    EngineControl       control;
    juce::SmoothedValue<double> timeRatioSmooth;

    bool    stealing = false;
    float   stealGain = 1.0f;
    float   stealStep = 0.0f;
    Pending pending;

    std::vector<std::vector<float>> scratch;
    std::vector<float>              noteBuf, ratioBuf;
    std::vector<float*>             scratchPtrs;
    std::vector<std::vector<float>> delayRing;   // [ch][kFixedLatency+8] 整列遅延
    int  delayPos = 0;
    int  delaySize = 0;
    int  preparedChannels = 0;
    int  preparedBlock    = 0;
};

} // namespace otomad
