#include "Voice.h"

#include <cmath>

namespace otomad
{

void Voice::prepare (double sr, int maxBlock, int numChannels)
{
    sampleRate       = sr;
    preparedChannels = juce::jmax (1, numChannels);
    preparedBlock    = juce::jmax (1, maxBlock);

    scratch.assign ((std::size_t) preparedChannels,
                    std::vector<float> ((std::size_t) preparedBlock, 0.0f));
    scratchPtrs.assign ((std::size_t) preparedChannels, nullptr);
    ratioBuf.assign ((std::size_t) preparedBlock, 1.0f);

    adsr.setSampleRate (sr);
    active = false;
}

void Voice::setAdsr (float attackSec, float decaySec, float sustain, float releaseSec) noexcept
{
    adsrParams.attack  = juce::jmax (0.0f, attackSec);
    adsrParams.decay   = juce::jmax (0.0f, decaySec);
    adsrParams.sustain = juce::jlimit (0.0f, 1.0f, sustain);
    adsrParams.release = juce::jmax (0.001f, releaseSec);
}

void Voice::noteOn (const SampleBuffer* sample, int note, float vel,
                    float s01, float e01, bool snap)
{
    if (sample == nullptr || sample->numSamples <= 0)
    {
        active = false;
        return;
    }

    midiNote = note;
    velocity = juce::jlimit (0.0f, 1.0f, vel);

    const auto n = sample->numSamples;
    const auto s = (std::int64_t) std::floor ((double) juce::jlimit (0.0f, 1.0f, s01) * (double) n);
    const auto e = (std::int64_t) std::ceil  ((double) juce::jlimit (0.0f, 1.0f, e01) * (double) n);
    reader.configure (sample, s, e, snap);

    srcPos = 0.0;
    sourceReleaseTriggered = false;

    adsr.setParameters (adsrParams);   // 発音時に確定（§3.8: 発音中は変えない）
    adsr.noteOn();
    active = true;
}

void Voice::noteOff() noexcept
{
    if (active)
        adsr.noteOff();
}

void Voice::stop() noexcept
{
    active = false;
    adsr.reset();
}

void Voice::render (float* const* out, int numChannels, int n) noexcept
{
    if (! active || n <= 0)
        return;

    const int nch = juce::jmin (numChannels, preparedChannels);

    // Phase 1 はブロック内一定のピッチ比（ポルタメントは Phase 2）
    const float semis = params.pitchSemi + params.pitchCents * 0.01f
                      + (float) (midiNote - params.rootKey);
    const float ratio = std::pow (2.0f, semis / 12.0f);
    for (int i = 0; i < n; ++i)
        ratioBuf[(std::size_t) i] = ratio;

    engine.setQuality (params.quality);
    for (int ch = 0; ch < nch; ++ch)
        scratchPtrs[(std::size_t) ch] = scratch[(std::size_t) ch].data();

    engine.process (reader, srcPos, scratchPtrs.data(), nch, n, ratioBuf.data());

    // 素材を読み切ったらリリース開始（ワンショットの語尾。Varispeed はテール0）
    if (! sourceReleaseTriggered && reader.isFinished (srcPos))
    {
        adsr.noteOff();
        sourceReleaseTriggered = true;
    }

    for (int i = 0; i < n; ++i)
    {
        const float env = adsr.getNextSample() * velocity * params.gainLin;
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] += scratch[(std::size_t) ch][(std::size_t) i] * env;
    }

    if (! adsr.isActive())
        active = false;
}

} // namespace otomad
