#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Params.h"
#include "core/SampleLoader.h"

#include <cmath>

using otomad::SampleBuffer;

//==============================================================================
OtoMadSamplerProcessor::OtoMadSamplerProcessor()
    : AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", otomad::params::createLayout())
{
    formatManager.registerBasicFormats();

    pPitchSemi   = apvts.getRawParameterValue (otomad::params::pitchSemi);
    pPitchCents  = apvts.getRawParameterValue (otomad::params::pitchCents);
    pRootKey     = apvts.getRawParameterValue (otomad::params::rootKey);
    pInterp      = apvts.getRawParameterValue (otomad::params::interpQuality);
    pAttack      = apvts.getRawParameterValue (otomad::params::attack);
    pDecay       = apvts.getRawParameterValue (otomad::params::decay);
    pSustain     = apvts.getRawParameterValue (otomad::params::sustain);
    pRelease     = apvts.getRawParameterValue (otomad::params::release);
    pSampleStart = apvts.getRawParameterValue (otomad::params::sampleStart);
    pSampleEnd   = apvts.getRawParameterValue (otomad::params::sampleEnd);
    pSnap        = apvts.getRawParameterValue (otomad::params::snapZeroCross);
    pGain        = apvts.getRawParameterValue (otomad::params::gain);
}

//==============================================================================
void OtoMadSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    voice.prepare (sampleRate, samplesPerBlock, 2);
    currentNote = -1;
}

bool OtoMadSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void OtoMadSamplerProcessor::updateVoiceParams() noexcept
{
    otomad::Voice::Params vp;
    vp.pitchSemi  = pPitchSemi->load();
    vp.pitchCents = pPitchCents->load();
    vp.rootKey    = (int) pRootKey->load();
    vp.gainLin    = juce::Decibels::decibelsToGain (pGain->load());
    vp.quality    = ((int) pInterp->load() == 0) ? otomad::VarispeedEngine::Quality::Linear
                                                 : otomad::VarispeedEngine::Quality::Hermite;
    voice.setParams (vp);

    voice.setAdsr (pAttack->load()  * 0.001f,
                   pDecay->load()   * 0.001f,
                   pSustain->load(),
                   pRelease->load() * 0.001f);
}

void OtoMadSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // 画面上のキーボードからの入力を MIDI にマージ
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    updateVoiceParams();

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
    if (tail > 0)
        renderSlice (buffer, pos, tail);
}

void OtoMadSamplerProcessor::renderSlice (juce::AudioBuffer<float>& buffer,
                                          int startSample, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    float* ptrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numCh; ++ch)
        ptrs[ch] = buffer.getWritePointer (ch) + startSample;

    voice.render (ptrs, numCh, numSamples);
}

void OtoMadSamplerProcessor::handleMidiMessage (const juce::MidiMessage& msg) noexcept
{
    if (msg.isNoteOn())
    {
        const int   note = msg.getNoteNumber();
        const float vel  = msg.getFloatVelocity();

        voice.noteOn (activeSample.load(), note, vel,
                      pSampleStart->load(), pSampleEnd->load(),
                      pSnap->load() > 0.5f);
        currentNote = note;
    }
    else if (msg.isNoteOff())
    {
        if (msg.getNoteNumber() == currentNote)
        {
            voice.noteOff();
            currentNote = -1;
        }
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        voice.stop();
        currentNote = -1;
    }
}

//==============================================================================
void OtoMadSamplerProcessor::loadSampleFromFile (const juce::File& file)
{
    const double sr = hostSampleRate.load();
    loadPool.addJob ([this, file, sr]
    {
        auto sb = otomad::SampleLoader::loadFile (file, sr, formatManager);
        if (sb == nullptr)
            return;

        {
            const juce::ScopedLock sl (graveyardLock);
            sampleGraveyard.push_back (sb);
        }
        activeSample.store (sb.get());
        sampleVersion.fetch_add (1);
    });
}

//==============================================================================
void OtoMadSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Phase 1 は APVTS のみ保存。サンプル埋め込みは Phase 5。
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OtoMadSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessorEditor* OtoMadSamplerProcessor::createEditor()
{
    return new OtoMadSamplerEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OtoMadSamplerProcessor();
}
