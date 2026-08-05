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
    pPortaMode   = apvts.getRawParameterValue (otomad::params::portaMode);
    pPortaShape  = apvts.getRawParameterValue (otomad::params::portaShape);
    pPortaTime   = apvts.getRawParameterValue (otomad::params::portaTime);
    pPortaCurve  = apvts.getRawParameterValue (otomad::params::portaCurve);
    pGlideGroup  = apvts.getRawParameterValue (otomad::params::glideGroupMs);
    pPolyMode    = apvts.getRawParameterValue (otomad::params::polyMode);
    pMaxVoices   = apvts.getRawParameterValue (otomad::params::maxVoices);
    pBendRange   = apvts.getRawParameterValue (otomad::params::bendRange);
}

//==============================================================================
void OtoMadSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    voices.prepare (sampleRate, samplesPerBlock, 2);
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
    voices.setVoiceParams (vp);

    voices.setAdsr (pAttack->load()  * 0.001f,
                    pDecay->load()   * 0.001f,
                    pSustain->load(),
                    pRelease->load() * 0.001f);

    voices.setPortamento ((otomad::VoiceManager::PortaMode) (int) pPortaMode->load(),
                          ((int) pPortaShape->load() == 0) ? otomad::PortamentoGenerator::Shape::Time
                                                           : otomad::PortamentoGenerator::Shape::Analog,
                          pPortaTime->load(), pPortaCurve->load(), pGlideGroup->load());

    voices.setPoly ((int) pPolyMode->load() == 1, (int) pMaxVoices->load());
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

    voices.render (ptrs, numCh, numSamples);
}

void OtoMadSamplerProcessor::handleMidiMessage (const juce::MidiMessage& msg) noexcept
{
    if (msg.isNoteOn())
    {
        voices.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), activeSample.load(),
                       pSampleStart->load(), pSampleEnd->load(), pSnap->load() > 0.5f);
    }
    else if (msg.isNoteOff())
    {
        voices.noteOff (msg.getNoteNumber());
    }
    else if (msg.isPitchWheel())
    {
        const float norm = ((float) msg.getPitchWheelValue() - 8192.0f) / 8192.0f;
        voices.setPitchBendSemi (norm * (float) (int) pBendRange->load());
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        voices.allNotesOff();
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
