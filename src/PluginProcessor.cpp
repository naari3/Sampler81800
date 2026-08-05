#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Params.h"
#include "core/SampleLoader.h"

#include <algorithm>
#include <cmath>

using otomad::SampleBuffer;

//==============================================================================
OtoMadSamplerProcessor::OtoMadSamplerProcessor()
    : AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", otomad::params::createLayout())
{
    formatManager.registerBasicFormats();
    pitchCache.setApi (&reaperApi);

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
    pAlgorithm   = apvts.getRawParameterValue (otomad::params::algorithm);
    pDurationMode = apvts.getRawParameterValue (otomad::params::durationMode);
    pSyncLength  = apvts.getRawParameterValue (otomad::params::syncLength);
    pStretch     = apvts.getRawParameterValue (otomad::params::stretchAmount);
    pFormant     = apvts.getRawParameterValue (otomad::params::formant);
    pPhaseLock   = apvts.getRawParameterValue (otomad::params::phaseLock);
    pReaperMode    = apvts.getRawParameterValue (otomad::params::reaperMode);
    pReaperSubMode = apvts.getRawParameterValue (otomad::params::reaperSubMode);
}

//==============================================================================
void OtoMadSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    voices.prepare (sampleRate, samplesPerBlock, 2, &reaperApi);
    lastReportedLatency = voices.getCurrentLatency();      // 既定(Varispeed)=0
    setLatencySamples (lastReportedLatency);
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
    vp.gainLin    = juce::Decibels::decibelsToGain (pGain->load()) * normGain.load();
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

    otomad::Voice::EngineControl ec;
    ec.algorithm    = (int) pAlgorithm->load();
    ec.durationMode = (int) pDurationMode->load();
    ec.stretchAmount = pStretch->load();
    static constexpr float syncBeatsTable[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    ec.syncBeats    = syncBeatsTable[juce::jlimit (0, 4, (int) pSyncLength->load())];
    ec.hostBpm      = hostBpm;
    ec.hostBpmValid = hostBpmValid;
    ec.formantSemi  = pFormant->load();
    ec.phaseLock    = pPhaseLock->load() > 0.5f;
    ec.reaperMode    = (int) pReaperMode->load();
    ec.reaperSubMode = (int) pReaperSubMode->load();
    voices.setEngineControl (ec);
}

void OtoMadSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // ホストテンポ（Sync 用, §4.7）
    hostBpmValid = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
            {
                hostBpm = *bpm;
                hostBpmValid = true;
            }

    // 画面上のキーボードからの入力を MIDI にマージ
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    updateVoiceParams();

    // レイテンシ報告（変化時のみ）。キャッシュ経路は Varispeed 再生なので 0。
    const int lat = useCachePath() ? 0 : voices.getCurrentLatency();
    if (lat != lastReportedLatency)
    {
        lastReportedLatency = lat;
        setLatencySamples (lat);
    }

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
        const int   note = msg.getNoteNumber();
        const float vel  = msg.getFloatVelocity();
        const float s = pSampleStart->load(), e = pSampleEnd->load();
        const bool  snap = pSnap->load() > 0.5f;

        if (useCachePath())
        {
            const int semi = juce::jlimit (otomad::PitchCache::kMin, otomad::PitchCache::kMax,
                                           note - (int) pRootKey->load() + (int) pPitchSemi->load());
            if (const auto* cached = pitchCache.lookup (semi))
                voices.noteOn (note, vel, cached, s, e, snap, true, (float) semi);   // 遅延ゼロ・élastique品質
            else
            {
                pitchCache.request (semi);                                           // 背景でレンダリング要求
                voices.noteOn (note, vel, activeSample.load(), s, e, snap, true, 0.0f); // 一発目は Varispeed で綺麗に
            }
        }
        else
        {
            voices.noteOn (note, vel, activeSample.load(), s, e, snap);
        }
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
void OtoMadSamplerProcessor::publishSample (std::shared_ptr<const otomad::SampleBuffer> sb)
{
    if (sb == nullptr)
        return;
    {
        const juce::ScopedLock sl (graveyardLock);
        sampleGraveyard.push_back (sb);
    }
    activeSample.store (sb.get());
    normGain.store (1.0f);
    sampleVersion.fetch_add (1);
}

void OtoMadSamplerProcessor::loadSampleFromFile (const juce::File& file)
{
    const double sr = hostSampleRate.load();
    loadPool.addJob ([this, file, sr]
    {
        if (auto sb = otomad::SampleLoader::loadFile (file, sr, formatManager))
            publishSample (sb);
    });
}

juce::StringArray OtoMadSamplerProcessor::getReaperModeNames() const
{
    juce::StringArray a;
    auto fn = reinterpret_cast<bool (*) (int, const char**)> (reaperApi.getFunction ("EnumPitchShiftModes"));
    if (fn == nullptr) return a;
    const char* mn = nullptr;
    for (int m = 0; m < 256 && fn (m, &mn); ++m)
        a.add (mn ? juce::String::fromUTF8 (mn) : ("mode " + juce::String (m)));
    return a;
}

juce::StringArray OtoMadSamplerProcessor::getReaperSubModeNames (int mode) const
{
    juce::StringArray a;
    auto fn = reinterpret_cast<const char* (*) (int, int)> (reaperApi.getFunction ("EnumPitchShiftSubModes"));
    if (fn == nullptr) return a;
    for (int s = 0; s < 100000; ++s)
    {
        const char* sn = fn (mode, s);
        if (sn == nullptr) break;
        a.add (juce::String::fromUTF8 (sn));
    }
    return a;
}

void OtoMadSamplerProcessor::serviceCache()
{
    // Manual のときは stretchAmount から timeRatio を算出（Natural は 1.0）
    double timeRatio = 1.0;
    if ((int) pDurationMode->load() == 2)
    {
        const float st = pStretch->load();
        timeRatio = st > 0.0f ? 1.0 / (double) st : 1.0;
    }

    // 素材/モード/フォルマント/ストレッチ を反映（変わっていれば ready を無効化して作り直す）
    const bool changed = pitchCache.configure (activeSample.load(), sampleVersion.load(),
                          (int) pReaperMode->load(), (int) pReaperSubMode->load(),
                          hostSampleRate.load(), pFormant->load(), timeRatio);

    // 設定確定時にプリウォーム: 現在の pitchSemi を中心に ±24 半音をまとめて背景生成（停止中に貯める）
    if (changed && useCachePath())
    {
        const int c = (int) pPitchSemi->load();
        pitchCache.requestRange (c - 24, c + 24);
    }

    // 保留中の音程があれば背景スレッドでレンダリング（多重起動しない）
    if (pitchCache.hasPending() && ! cacheJobRunning.exchange (true))
        loadPool.addJob ([this]
        {
            while (pitchCache.renderPending()) {}
            cacheJobRunning.store (false);
        });
}

void OtoMadSamplerProcessor::reconfigureReaperMode()
{
    const int m  = (int) pReaperMode->load();
    const int sm = (int) pReaperSubMode->load();
    suspendProcessing (true);          // オーディオコールバックを止めてから非RT処理
    voices.reconfigureReaper (m, sm);
    lastReportedLatency = voices.getCurrentLatency();
    setLatencySamples (lastReportedLatency);
    suspendProcessing (false);
}

void OtoMadSamplerProcessor::normalizeSample()
{
    const auto* sb = activeSample.load();
    if (sb == nullptr || sb->numChannels <= 0)
        return;

    float peak = 0.0f;
    for (int ch = 0; ch < sb->numChannels; ++ch)
        for (float v : sb->data[(std::size_t) ch])
            peak = std::max (peak, std::abs (v));

    normGain.store (peak > 1.0e-6f ? std::min (0.99f / peak, 64.0f) : 1.0f);
    sampleVersion.fetch_add (1);   // 波形表示を更新させる
}

//==============================================================================
void OtoMadSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto root = std::make_unique<juce::XmlElement> ("OtoMadState");

    if (auto apvtsXml = apvts.copyState().createXml())
        root->addChildElement (apvtsXml.release());

    // サンプルを埋め込み保存（FLAC）＋パス（§3.9）
    if (const auto* sb = activeSample.load())
    {
        auto* se = root->createNewChildElement ("sample");
        se->setAttribute ("name", juce::String (sb->name));
        se->setAttribute ("path", juce::String (sb->path));

        juce::MemoryBlock flacData;
        float normScale = 1.0f;
        if (otomad::SampleLoader::encodeOriginalToFlac (*sb, flacData, normScale))
        {
            se->setAttribute ("embedded", 1);
            se->setAttribute ("format", "flac");
            se->setAttribute ("srcSampleRate", sb->originalSampleRate);
            se->setAttribute ("normScale", (double) normScale);
            se->addTextElement (flacData.toBase64Encoding());
        }
        else
        {
            se->setAttribute ("embedded", 0);
        }
    }

    copyXmlToBinary (*root, destData);
}

void OtoMadSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    juce::XmlElement* apvtsXml  = nullptr;
    juce::XmlElement* sampleXml = nullptr;

    if (xml->hasTagName (apvts.state.getType()))
        apvtsXml = xml.get();                                   // 旧形式（APVTSのみ）
    else if (xml->hasTagName ("OtoMadState"))
    {
        apvtsXml  = xml->getChildByName (apvts.state.getType());
        sampleXml = xml->getChildByName ("sample");
    }

    if (apvtsXml != nullptr)
        apvts.replaceState (juce::ValueTree::fromXml (*apvtsXml));

    if (sampleXml != nullptr)
        restoreSample (*sampleXml);
}

void OtoMadSamplerProcessor::restoreSample (const juce::XmlElement& se)
{
    const double sr = hostSampleRate.load();
    std::shared_ptr<otomad::SampleBuffer> sb;

    // 埋め込み優先
    if ((int) se.getIntAttribute ("embedded", 0) == 1)
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (se.getAllSubText()))
            sb = otomad::SampleLoader::loadFromFlacMemory (mb.getData(), mb.getSize(), sr);

        if (sb != nullptr)
        {
            const float scale = (float) se.getDoubleAttribute ("normScale", 1.0);
            if (scale != 1.0f)   // 保存時に正規化した分を戻す
            {
                for (auto& ch : sb->original) for (auto& v : ch) v *= scale;
                for (auto& ch : sb->data)     for (auto& v : ch) v *= scale;
                for (auto& p : sb->peaks) { p.first *= scale; p.second *= scale; }
            }
        }
    }

    // ダメならパスから
    if (sb == nullptr)
    {
        const juce::String path = se.getStringAttribute ("path");
        const juce::File f (path);
        if (path.isNotEmpty() && f.existsAsFile())
            sb = otomad::SampleLoader::loadFile (f, sr, formatManager);
    }

    if (sb != nullptr)
    {
        sb->name = se.getStringAttribute ("name").toStdString();
        sb->path = se.getStringAttribute ("path").toStdString();
        publishSample (sb);
    }
    // 両方失敗 → サンプル無しのまま（GUI表示のみ、クラッシュしない, §3.9）
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
