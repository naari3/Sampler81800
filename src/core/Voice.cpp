#include "Voice.h"
#include "host/ReaperApi.h"

#include <algorithm>
#include <cmath>

namespace otomad
{

void Voice::prepare (double sr, int maxBlock, int numChannels,
                     EngineResources& resources, host::ReaperApi* reaperApi)
{
    sampleRate       = sr;
    preparedChannels = juce::jmax (1, numChannels);
    preparedBlock    = juce::jmax (1, maxBlock);

    scratch.assign ((std::size_t) preparedChannels,
                    std::vector<float> ((std::size_t) preparedBlock, 0.0f));
    scratchPtrs.assign ((std::size_t) preparedChannels, nullptr);
    noteBuf.assign ((std::size_t) preparedBlock, 0.0f);
    ratioBuf.assign ((std::size_t) preparedBlock, 1.0f);

    const PitchEngineContext ctx { sr, preparedBlock, preparedChannels };
    varispeed.prepare (ctx, resources);
    wsola.prepare (ctx, resources);
    phaseVocoder.prepare (ctx, resources);
    reaper.setReaperApi (reaperApi);
    reaper.prepare (ctx, resources);   // 非REAPERなら isAvailable()==false のまま
    activeEngine = &varispeed;

    adsr.setSampleRate (sr);
    porta.setSampleRate (sr);
    timeRatioSmooth.reset (sr, 0.02);
    timeRatioSmooth.setCurrentAndTargetValue (1.0);
    active = false;
    stealing = false;
    stealGain = 1.0f;
}

void Voice::setAdsr (float a, float d, float s, float r) noexcept
{
    adsrParams.attack  = juce::jmax (0.0f, a);
    adsrParams.decay   = juce::jmax (0.0f, d);
    adsrParams.sustain = juce::jlimit (0.0f, 1.0f, s);
    adsrParams.release = juce::jmax (0.001f, r);
}

void Voice::setPortamentoConfig (PortamentoGenerator::Shape shape, float timeMs, float curve) noexcept
{
    porta.setShape (shape);
    porta.setTime (timeMs);
    porta.setCurve (curve);
}

IPitchEngine* Voice::pickEngine (int algorithm) noexcept
{
    switch (algorithm)
    {
        case 0: fallbackActive = false; return &varispeed;
        case 1: fallbackActive = false; return &wsola;
        case 2: fallbackActive = false; return &phaseVocoder;
        case 5:                         // REAPER Shifter: REAPER上でのみ実動作
            if (reaper.isAvailable()) { fallbackActive = false; return &reaper; }
            fallbackActive = true;    return &phaseVocoder;   // 規約2: 代替(PV)へ
        default:                        // 3,4 は未実装 → 規約2
            fallbackActive = true;    return &phaseVocoder;
    }
}

void Voice::setEngineControl (const EngineControl& c) noexcept
{
    control = c;
    reaper.setMode (c.reaperMode);
    reaper.setSubMode (c.reaperSubMode);
    activeEngine = pickEngine (c.algorithm);
    phaseVocoder.setPhaseLock (c.phaseLock);
}

double Voice::resolveTimeRatio() noexcept
{
    if (activeEngine == nullptr || ! activeEngine->preservesDuration())
        return 1.0;   // Varispeed

    switch (control.durationMode)
    {
        case 2: // Manual
            return control.stretchAmount > 0.0f ? 1.0 / (double) control.stretchAmount : 1.0;
        case 1: // Sync
        {
            if (! control.hostBpmValid) return 1.0;
            const double targetSec = (double) control.syncBeats * 60.0 / control.hostBpm;
            const double srcSec    = reader.getTrimmedLengthSeconds();
            if (srcSec <= 0.0 || targetSec <= 0.0) return 1.0;
            return juce::jlimit (0.25, 4.0, srcSec / targetSec);
        }
        default: return 1.0; // Natural
    }
}

void Voice::startNote (const Pending& p) noexcept
{
    if (p.sample == nullptr || p.sample->numSamples <= 0)
    {
        active = false;
        return;
    }

    midiNote = p.note;
    velocity = juce::jlimit (0.0f, 1.0f, p.vel);

    const auto n = p.sample->numSamples;
    const auto s = (std::int64_t) std::floor ((double) juce::jlimit (0.0f, 1.0f, p.s01) * (double) n);
    const auto e = (std::int64_t) std::ceil  ((double) juce::jlimit (0.0f, 1.0f, p.e01) * (double) n);
    reader.configure (p.sample, s, e, p.snap);

    srcPos = 0.0;
    sourceReleaseTriggered = false;
    released = false;
    drainCounter = 0;

    varispeed.reset();
    wsola.reset();
    phaseVocoder.reset();
    reaper.reset();

    if (p.glide) porta.startGlide (p.originNote, (float) p.note);
    else         porta.startAt ((float) p.note);

    adsr.setParameters (adsrParams);

    // エンベロープ開始をエンジンのレイテンシ分だけ遅らせる（音の出力遅延と揃える）
    const int lat = activeEngine ? activeEngine->getIntrinsicLatency() : 0;
    pendingOff = -1;
    if (lat <= 0)
    {
        adsr.noteOn();
        pendingOn = -1;
    }
    else
    {
        adsr.reset();       // 遅延中はアイドル（音も無音なので整合）
        pendingOn = lat;
    }

    active = true;
    stealing = false;
    stealGain = 1.0f;
}

void Voice::noteOn (const SampleBuffer* sample, int note, float vel,
                    float s01, float e01, bool snap, bool glide, float originNote)
{
    startNote (Pending { sample, note, vel, s01, e01, snap, glide, originNote });
}

void Voice::requestSteal (const SampleBuffer* sample, int note, float vel,
                          float s01, float e01, bool snap, bool glide, float originNote)
{
    Pending p { sample, note, vel, s01, e01, snap, glide, originNote };
    if (! active) { startNote (p); return; }
    pending   = p;
    stealing  = true;
    stealGain = 1.0f;
    stealStep = -(float) (1.0 / (0.005 * sampleRate));
}

void Voice::glideTo (int note) noexcept
{
    if (! active) return;
    porta.setTarget ((float) note);
    midiNote = note;
    released = false;
}

void Voice::setGlideOrigin (float originNote) noexcept { porta.setOrigin (originNote); }

void Voice::noteOff() noexcept
{
    if (active && ! stealing)
    {
        const int lat = activeEngine ? activeEngine->getIntrinsicLatency() : 0;
        if (lat <= 0) adsr.noteOff();
        else          pendingOff = lat;   // リリースもレイテンシ分遅らせる
        released = true;
    }
}

void Voice::stop() noexcept
{
    active = false; stealing = false; stealGain = 1.0f;
    pendingOn = -1; pendingOff = -1;
    adsr.reset();
}

void Voice::render (float* const* out, int numChannels, int n) noexcept
{
    if (! active || n <= 0)
        return;

    const int nch = juce::jmin (numChannels, preparedChannels);

    // 遅延したエンベロープイベントの発火（ブロック粒度）。音の出力遅延に揃える。
    if (pendingOn >= 0)
    {
        if (pendingOn <= n) { adsr.noteOn(); pendingOn = -1; }
        else                  pendingOn -= n;
    }
    if (pendingOff >= 0)
    {
        if (pendingOff <= n) { adsr.noteOff(); pendingOff = -1; }
        else                   pendingOff -= n;
    }

    // timeRatio（20msスムージング, §4.7）
    timeRatioSmooth.setTargetValue (resolveTimeRatio());
    const double tr = timeRatioSmooth.skip (n);

    activeEngine->setFormantShift (control.formantSemi);
    varispeed.setQuality (params.quality);

    // ピッチ（ノート番号 → 比）
    porta.process (noteBuf.data(), n);
    const float base = params.pitchSemi + params.pitchCents * 0.01f
                     + params.pitchBendSemi - (float) params.rootKey;
    for (int i = 0; i < n; ++i)
        ratioBuf[(std::size_t) i] = std::exp2 ((noteBuf[(std::size_t) i] + base) / 12.0f);

    for (int ch = 0; ch < nch; ++ch)
        scratchPtrs[(std::size_t) ch] = scratch[(std::size_t) ch].data();

    activeEngine->process (reader, srcPos, scratchPtrs.data(), nch, n, ratioBuf.data(), tr);

    // 素材を読み切ったらリリース開始（レイテンシ分遅らせる）+ テールドレイン計測
    const bool srcDone = reader.isFinished (srcPos);
    if (! sourceReleaseTriggered && srcDone)
    {
        const int lat = activeEngine->getIntrinsicLatency();
        if (lat <= 0)            adsr.noteOff();
        else if (pendingOff < 0) pendingOff = lat;
        released = true; sourceReleaseTriggered = true;
    }
    if (srcDone) drainCounter += n; else drainCounter = 0;

    // エンベロープ + ゲイン（レイテンシ整列は廃止、エンジン出力をそのまま加算）
    for (int i = 0; i < n; ++i)
    {
        float g = adsr.getNextSample() * velocity * params.gainLin;
        if (stealing)
        {
            g *= stealGain;
            stealGain += stealStep;
            if (stealGain < 0.0f) stealGain = 0.0f;
        }
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] += scratch[(std::size_t) ch][(std::size_t) i] * g;
    }

    if (stealing && stealGain <= 0.0f)
    {
        startNote (pending);
        return;
    }

    // 素材を読み切った後、エンジン内部テール(getTailSamples)を全量ドレインしてから停止（切らない）。
    // 遅延エンベロープ発火待ち(pendingOn/Off)の間は落とさない（音がこれから出るため）。
    const bool envPending = (pendingOn >= 0 || pendingOff >= 0);
    const int  totalTail  = activeEngine->getTailSamples();
    if (! stealing && ! envPending && ! adsr.isActive() && (! srcDone || drainCounter >= totalTail))
        active = false;
}

int Voice::getReportedLatency (int algorithm) const noexcept
{
    switch (algorithm)
    {
        case 0: return varispeed.getIntrinsicLatency();        // 0（生演奏で低遅延）
        case 1: return wsola.getIntrinsicLatency();
        case 2: return phaseVocoder.getIntrinsicLatency();
        case 5: return reaper.isAvailable() ? reaper.getIntrinsicLatency()
                                            : phaseVocoder.getIntrinsicLatency();
        default: return phaseVocoder.getIntrinsicLatency();    // 3,4 は PV フォールバック
    }
}

} // namespace otomad
