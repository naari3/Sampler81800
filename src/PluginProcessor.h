#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "core/SampleBuffer.h"
#include "core/VoiceManager.h"
#include "host/ReaperApi.h"
#include "pitch/PitchCache.h"

namespace otomad { }

//==============================================================================
/**
    OtoMadSampler — Phase 1（最小の音MADサンプラー）。

    D&D読み込み → 原音保持＋ホストSR変換、SourceReader(トリム/ゼロクロス吸着)、
    VarispeedEngine(Linear/Hermite)、pitch/rootKey/ADSR、モノフォニック。
    §2.2 の MIDIサブブロック分割は Phase 0 から維持。
*/
class OtoMadSamplerProcessor : public juce::AudioProcessor,
                               public juce::VST3ClientExtensions
{
public:
    OtoMadSamplerProcessor();
    ~OtoMadSamplerProcessor() override = default;

    // VST3 経由で REAPER ホストAPIを取得する（§5.2）。メッセージスレッドで呼ばれる。
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    void setIHostApplication (Steinberg::FUnknown* ptr) override { reaperApi.attachFromVST3 ((void*) ptr); }

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return "OtoMadSampler"; }
    bool acceptsMidi()  const override { return true;  }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // GUI 用アクセサ
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    juce::MidiKeyboardState& getKeyboardState() noexcept    { return keyboardState; }
    const otomad::SampleBuffer* getActiveSample() const noexcept { return activeSample.load(); }
    int  getSampleVersion() const noexcept { return sampleVersion.load(); }
    bool isEngineFallbackActive() const noexcept { return voices.isFallbackActive(); }
    bool isReaperAvailable() const noexcept { return reaperApi.isAvailable(); }

    // GUI表示用: 現在の REAPER モード名/サブモード名/レイテンシ/個数
    juce::String getReaperModeText() const
    {
        return juce::String (voices.getReaperModeName().c_str()) + " / "
             + juce::String (voices.getReaperSubModeName().c_str())
             + "  (lat=" + juce::String (voices.getReaperLatency())
             + ", modes=" + juce::String (voices.getReaperModeCount())
             + ", subs=" + juce::String (voices.getReaperSubModeCount()) + ")";
    }

    // ノーマライズ: 現在のサンプルのピークから正規化ゲインを算出（メッセージスレッド）。
    void  normalizeSample();
    void  resetNormalize() noexcept { normGain.store (1.0f); }
    float getNormGain() const noexcept { return normGain.load(); }

    // メッセージスレッドから呼ぶ。バックグラウンドで読み込み、完了後にアトミック公開。
    void loadSampleFromFile (const juce::File& file);

    // REAPERシフタのモード変更を反映（メッセージスレッド, suspend下で再設定＋レイテンシ再報告）。
    void reconfigureReaperMode();

    // ピッチキャッシュの保守（メッセージスレッド, Editorのタイマーから）。
    void serviceCache();

    // コンボボックス用: REAPER のモード名 / サブモード名を列挙（メッセージスレッド）。
    juce::StringArray getReaperModeNames() const;
    juce::StringArray getReaperSubModeNames (int mode) const;

private:
    //==========================================================================
    void renderSlice (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void handleMidiMessage (const juce::MidiMessage& msg) noexcept;
    void updateVoiceParams() noexcept;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState            keyboardState;

    juce::AudioFormatManager formatManager;
    juce::ThreadPool         loadPool { 1 };

    otomad::VoiceManager voices;
    otomad::host::ReaperApi reaperApi;
    otomad::PitchCache      pitchCache;
    std::atomic<bool>       cacheJobRunning { false };

    // REAPER Shifter + Natural + formant0 のときはキャッシュ経路（Varispeed再生）を使う。
    bool useCachePath() const noexcept
    {
        const float f = pFormant->load();
        return (int) pAlgorithm->load() == 5
            && (int) pDurationMode->load() == 0
            && f < 0.01f && f > -0.01f;
    }

    std::atomic<double> hostSampleRate { 44100.0 };

    // ロックフリー・サンプルスロット。旧バッファは Phase 1 では graveyard で寿命を延ばす
    // （メッセージスレッド専用。Phase 5 で GCスレッド化する）。
    std::atomic<const otomad::SampleBuffer*> activeSample { nullptr };
    std::atomic<int>   sampleVersion { 0 };
    std::atomic<float> normGain { 1.0f };   // ノーマライズ倍率（gain とは別に掛かる）
    std::vector<std::shared_ptr<const otomad::SampleBuffer>> sampleGraveyard;
    juce::CriticalSection graveyardLock;

    // RTアクセス用にキャッシュしたパラメータ atomic
    std::atomic<float>* pPitchSemi   = nullptr;
    std::atomic<float>* pPitchCents  = nullptr;
    std::atomic<float>* pRootKey     = nullptr;
    std::atomic<float>* pInterp      = nullptr;
    std::atomic<float>* pAttack      = nullptr;
    std::atomic<float>* pDecay       = nullptr;
    std::atomic<float>* pSustain     = nullptr;
    std::atomic<float>* pRelease     = nullptr;
    std::atomic<float>* pSampleStart = nullptr;
    std::atomic<float>* pSampleEnd   = nullptr;
    std::atomic<float>* pSnap        = nullptr;
    std::atomic<float>* pGain        = nullptr;
    std::atomic<float>* pPortaMode   = nullptr;
    std::atomic<float>* pPortaShape  = nullptr;
    std::atomic<float>* pPortaTime   = nullptr;
    std::atomic<float>* pPortaCurve  = nullptr;
    std::atomic<float>* pGlideGroup  = nullptr;
    std::atomic<float>* pPolyMode    = nullptr;
    std::atomic<float>* pMaxVoices   = nullptr;
    std::atomic<float>* pBendRange   = nullptr;
    std::atomic<float>* pAlgorithm   = nullptr;
    std::atomic<float>* pDurationMode = nullptr;
    std::atomic<float>* pSyncLength  = nullptr;
    std::atomic<float>* pStretch     = nullptr;
    std::atomic<float>* pFormant     = nullptr;
    std::atomic<float>* pPhaseLock   = nullptr;
    std::atomic<float>* pReaperMode    = nullptr;
    std::atomic<float>* pReaperSubMode = nullptr;

    double hostBpm = 120.0;
    bool   hostBpmValid = false;
    int    lastReportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerProcessor)
};
