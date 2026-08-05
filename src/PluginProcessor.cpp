#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
OtoMadSamplerProcessor::OtoMadSamplerProcessor()
    : AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
void OtoMadSamplerProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    // 5ms フェード。ノートオン/オフ時の段差でクリックが出ないように。
    fadePerSample = (float) (1.0 / (0.005 * sampleRate));

    for (auto& v : voices)
        v = SineVoice {};
}

//==============================================================================
bool OtoMadSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // インストゥルメントなので入力は不要。出力は mono / stereo のみ受ける。
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void OtoMadSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // ---- §2.2 : MIDIイベント位置でブロックを分割してレンダリング ----
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        if (t > pos)
        {
            renderSlice (buffer, pos, t - pos);
            pos = t;
        }
        handleMidiMessage (meta.getMessage());
    }

    const int tail = buffer.getNumSamples() - pos;
    if (tail > 0)                       // 末尾区間。n==0 では呼ばない (§2.2)
        renderSlice (buffer, pos, tail);
}

//==============================================================================
void OtoMadSamplerProcessor::renderSlice (juce::AudioBuffer<float>& buffer,
                                          int startSample, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int   numCh    = buffer.getNumChannels();
    const double twoPi   = juce::MathConstants<double>::twoPi;
    constexpr float headroom = 0.2f;    // 単純な和音でクリップしない程度

    for (auto& v : voices)
    {
        // 完全に沈黙している（リリース済み）ボイスはスキップ
        if (v.note < 0 && v.level <= 0.0f)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            // 目標ゲインへ1サンプルあたり fadePerSample まで近づける
            const float d = juce::jlimit (-fadePerSample, fadePerSample, v.target - v.level);
            v.level += d;

            const float s = (float) std::sin (v.phase) * v.level * headroom;

            v.phase += v.phaseInc;
            if (v.phase >= twoPi)
                v.phase -= twoPi;

            for (int ch = 0; ch < numCh; ++ch)
                buffer.addSample (ch, startSample + i, s);
        }

        // リリースが終わり切ったら回収
        if (v.note < 0 && v.level <= 0.0f)
            v = SineVoice {};
    }
}

//==============================================================================
void OtoMadSamplerProcessor::handleMidiMessage (const juce::MidiMessage& msg) noexcept
{
    if (msg.isNoteOn())
    {
        const int note = msg.getNoteNumber();

        // 同一ノート再発音 → 空き → どちらも無ければボイス0を奪う
        SineVoice* slot = nullptr;
        for (auto& v : voices) if (v.note == note) { slot = &v; break; }
        if (slot == nullptr)
            for (auto& v : voices) if (v.note < 0) { slot = &v; break; }
        if (slot == nullptr)
            slot = &voices[0];

        slot->note     = note;
        slot->phaseInc = juce::MathConstants<double>::twoPi
                       * juce::MidiMessage::getMidiNoteInHertz (note) / currentSampleRate;
        slot->target   = juce::jlimit (0.0f, 1.0f, msg.getFloatVelocity());
        // phase / level は保持（ボイスを奪ったときの波形連続性のため）
    }
    else if (msg.isNoteOff())
    {
        const int note = msg.getNoteNumber();
        for (auto& v : voices)
            if (v.note == note) { v.note = -1; v.target = 0.0f; }
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        for (auto& v : voices) { v.note = -1; v.target = 0.0f; }
    }
}

//==============================================================================
juce::AudioProcessorEditor* OtoMadSamplerProcessor::createEditor()
{
    return new OtoMadSamplerEditor (*this);
}

//==============================================================================
// JUCE のプラグインエントリポイント
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OtoMadSamplerProcessor();
}
