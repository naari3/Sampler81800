#include "VoiceManager.h"
#include "VoiceAllocator.h"
#include "GlideMatcher.h"

namespace otomad
{

void VoiceManager::prepare (double sr, int maxBlock, int numChannels, host::ReaperApi* reaperApi)
{
    sampleRate = sr;
    resources.prepare (sr);
    for (auto& v : voices)
        v.prepare (sr, maxBlock, numChannels, resources, reaperApi);
    voiceOnTime.fill (0);
    sampleCounter = 0;
    lastNoteOnTime = 0;
    monoNote = -1;
    glidePool.clear();
    currentGroup.clear();
}

void VoiceManager::setPortamento (PortaMode mode, PortamentoGenerator::Shape shape,
                                  float timeMs, float curve, float gMs) noexcept
{
    portaMode  = mode;
    portaShape = shape;
    portaTimeMs = timeMs;
    portaCurve  = curve;
    groupMs     = gMs;
}

std::uint64_t VoiceManager::groupWindowSamples() const noexcept
{
    return (std::uint64_t) juce::jmax (0.0, (double) groupMs * 0.001 * sampleRate);
}

void VoiceManager::snapshotGlidePool()
{
    glidePool.clear();
    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing())
            glidePool.push_back (v.currentPitchNote());
}

void VoiceManager::rematchGroupOrigins()
{
    if (glidePool.empty() || currentGroup.empty())
        return;

    std::vector<float> newPitches;
    newPitches.reserve (currentGroup.size());
    for (const auto& g : currentGroup)
        newPitches.push_back ((float) g.note);

    const auto match = computeGlideMatching (glidePool, newPitches);
    for (std::size_t i = 0; i < currentGroup.size(); ++i)
    {
        const int oi = match[i];
        if (oi >= 0)
            voices[(std::size_t) currentGroup[i].slot].setGlideOrigin (glidePool[(std::size_t) oi]);
        // 対応が無い声部はグライドなし（即時発音時の startAt のまま）
    }
}

void VoiceManager::noteOn (int note, float vel, const SampleBuffer* sample,
                           float s01, float e01, bool snap)
{
    const auto now = sampleCounter;

    if (! poly)
    {
        Voice& v = voices[0];
        const bool legato  = v.isActive() && ! v.isReleasing();
        const bool doGlide = (portaMode != PortaMode::Off) && legato;

        if (doGlide)
            v.glideTo (note);                                   // ピッチのみ滑らす
        else
            v.noteOn (sample, note, vel, s01, e01, snap, false, (float) note);

        voiceOnTime[0] = now;
        monoNote = note;
        lastNoteOnTime = now;
        return;
    }

    // ---- Poly ----
    if (portaMode != PortaMode::Off && (now - lastNoteOnTime) > groupWindowSamples())
    {
        snapshotGlidePool();     // 新しい和音グループの始まり
        currentGroup.clear();
    }
    lastNoteOnTime = now;

    // 割り当て判定（maxVoices 個の範囲で）
    const int limit = juce::jlimit (1, kMaxVoices, maxVoices);
    std::vector<VoiceSlotState> states ((std::size_t) limit);
    for (int i = 0; i < limit; ++i)
        states[(std::size_t) i] = { voices[(std::size_t) i].isActive(),
                                    voices[(std::size_t) i].getNote(),
                                    voiceOnTime[(std::size_t) i],
                                    voices[(std::size_t) i].isReleasing() };

    const int slot = chooseVoiceSlot (states, note);
    Voice& v = voices[(std::size_t) slot];

    const bool steal = v.isActive() && v.getNote() != note;
    if (steal)
        v.requestSteal (sample, note, vel, s01, e01, snap, false, (float) note);
    else
        v.noteOn (sample, note, vel, s01, e01, snap, false, (float) note);

    voiceOnTime[(std::size_t) slot] = now;

    if (portaMode != PortaMode::Off)
    {
        currentGroup.push_back ({ slot, note });
        rematchGroupOrigins();
    }
}

void VoiceManager::noteOff (int note)
{
    if (! poly)
    {
        if (note == monoNote)
        {
            voices[0].noteOff();
            monoNote = -1;
        }
        return;
    }

    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing() && v.getNote() == note)
            v.noteOff();
}

void VoiceManager::allNotesOff()
{
    for (auto& v : voices)
        v.stop();
    monoNote = -1;
    glidePool.clear();
    currentGroup.clear();
}

void VoiceManager::render (float* const* out, int numChannels, int n)
{
    Voice::Params p = baseParams;
    p.pitchBendSemi = bendSemi;

    const int limit = poly ? juce::jlimit (1, kMaxVoices, maxVoices) : 1;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(std::size_t) i];
        v.setParams (p);
        v.setAdsr (aA, aD, aS, aR);
        v.setPortamentoConfig (portaShape, portaTimeMs, portaCurve);
        v.setEngineControl (engineControl);
        if (i < limit)
            v.render (out, numChannels, n);
        else if (v.isActive())
            v.stop();     // maxVoices を下げたときに余剰ボイスを止める
    }

    sampleCounter += (std::uint64_t) n;
}

} // namespace otomad
