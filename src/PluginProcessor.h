#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    OtoMadSampler — Phase 0 の土台。

    この時点の実体は「MIDIノート→サイン波」の暫定シンセにすぎない。
    重要なのは中身ではなく **§2.2 の MIDIサブブロック分割の器を最初から入れておく**こと。
    Phase 1 以降で SampleBuffer / SourceReader / Voice / IPitchEngine に置き換えても、
    processBlock の分割構造はそのまま残す（後から入れると全体に波及するため）。
*/
class OtoMadSamplerProcessor : public juce::AudioProcessor
{
public:
    OtoMadSamplerProcessor();
    ~OtoMadSamplerProcessor() override = default;

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
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    //==========================================================================
    // §2.2 : MIDIイベント位置でブロックを分割し、区間ごとに全ボイスをレンダリングする。
    void renderSlice (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void handleMidiMessage (const juce::MidiMessage& msg) noexcept;

    // Phase 0 の暫定サイン波ボイス。Phase 1 以降で core/Voice に置き換わる。
    struct SineVoice
    {
        int    note     = -1;    // -1 = 非アクティブ
        double phase    = 0.0;   // [0, 2π)
        double phaseInc = 0.0;
        float  level    = 0.0f;  // 現在ゲイン（簡易フェードでクリック低減）
        float  target   = 0.0f;  // 目標ゲイン
    };

    static constexpr int kNumVoices = 16;
    std::array<SineVoice, kNumVoices> voices;

    double currentSampleRate = 44100.0;
    float  fadePerSample     = 0.0f;   // 1サンプルあたりのゲイン変化量（5ms フェード）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerProcessor)
};
