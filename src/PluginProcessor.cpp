#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Params.h"
#include "core/SampleLoader.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

using otomad::SampleBuffer;

//==============================================================================
// 外観ブロードキャスト用のプロセス内レジストリ。同一プロセスで動く全インスタンスを束ね、
// 「既定にする」押下時に現在の外観を全インスタンスへ即時反映する（メッセージスレッド）。
namespace
{
    struct AppearanceHub
    {
        std::mutex m;
        std::vector<OtoMadSamplerProcessor*> instances;
        static AppearanceHub& get() { static AppearanceHub h; return h; }
    };
}

//==============================================================================
OtoMadSamplerProcessor::OtoMadSamplerProcessor()
    : AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", otomad::params::createLayout())
{
    formatManager.registerBasicFormats();
    pitchCache.setApi (&reaperApi);

    pPitchSemi   = apvts.getRawParameterValue (otomad::params::pitchSemi);
    pOctave      = apvts.getRawParameterValue (otomad::params::octave);
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
    pTailMode      = apvts.getRawParameterValue (otomad::params::tailMode);
    pTailPercent   = apvts.getRawParameterValue (otomad::params::tailPercent);
    pTailMs        = apvts.getRawParameterValue (otomad::params::tailMs);
    pTailSyncDiv   = apvts.getRawParameterValue (otomad::params::tailSyncDiv);

    pendingOff.fill (-1);

    loadDefaultAppearance();   // 全インスタンス共通の外観既定（あれば）。state復元があれば後で上書きされる。

    // ブロードキャスト用レジストリに登録
    {
        auto& hub = AppearanceHub::get();
        std::lock_guard<std::mutex> lk (hub.m);
        hub.instances.push_back (this);
    }

    startTimerHz (6);   // UI非依存でキャッシュを駆動（窓を閉じても貯まる）
}

OtoMadSamplerProcessor::~OtoMadSamplerProcessor()
{
    stopTimer();
    auto& hub = AppearanceHub::get();
    std::lock_guard<std::mutex> lk (hub.m);
    hub.instances.erase (std::remove (hub.instances.begin(), hub.instances.end(), this),
                         hub.instances.end());
}

//==============================================================================
void OtoMadSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    voices.prepare (sampleRate, samplesPerBlock, 2, &reaperApi);
    lastReportedLatency = voices.getCurrentLatency();      // 既定(Varispeed)=0
    setLatencySamples (lastReportedLatency);

    transportSample = 0;         // Tail 用クロック/状態をリセット
    pendingOff.fill (-1);

    prepared.store (true);   // ここで state 復元の有無が確定（setStateInformation は prepare より前）
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
    vp.pitchSemi  = pPitchSemi->load() + 12.0f * pOctave->load();   // オクターブを半音に換算して合算
    vp.pitchCents = pPitchCents->load();
    vp.rootKey    = (int) pRootKey->load();
    vp.gainLin    = juce::Decibels::decibelsToGain (pGain->load()) * normGain.load();
    vp.quality    = otomad::VarispeedEngine::Quality::Hermite;   // 補間は Hermite 固定（良い方）
    voices.setVoiceParams (vp);

    voices.setAdsr (pAttack->load()  * 0.001f,
                    pDecay->load()   * 0.001f,
                    pSustain->load(),
                    pRelease->load() * 0.001f);

    voices.setPortamento ((otomad::VoiceManager::PortaMode) (int) pPortaMode->load(),
                          otomad::PortamentoGenerator::Shape::Time,   // Shape は Time 固定
                          pPortaTime->load(), pPortaCurve->load(), pGlideGroup->load());

    // Poly/Mono は Voices で決める: 1 なら Mono（ラストノート・グライド）、2以上で Poly。
    const int maxV = (int) pMaxVoices->load();
    voices.setPoly (maxV > 1, maxV);

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

    // Tail: 期限が来た遅延ノートオフを発火（ブロック粒度。数ms未満の誤差は許容）
    fireDueTailOffs (transportSample);

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
        handleMidiMessage (meta.getMessage(), transportSample + t);
    }

    const int tail = buffer.getNumSamples() - pos;
    if (tail > 0)
        renderSlice (buffer, pos, tail);

    transportSample += buffer.getNumSamples();
}

// Tail 自動ノートオフのうち、発火予定サンプルがこのブロック開始以前になったものを発火する。
void OtoMadSamplerProcessor::fireDueTailOffs (std::int64_t blockStart) noexcept
{
    for (int n = 0; n < kNumNotes; ++n)
        if (pendingOff[(std::size_t) n] >= 0 && pendingOff[(std::size_t) n] <= blockStart)
        {
            voices.noteOff (n);
            pendingOff[(std::size_t) n] = -1;
        }
}

// このノートを鳴らしたときの「再生の長さ」（出力SRサンプル数）を発音時点で見積もる。
// キャッシュ経路は事前レンダ済みバッファ長が正確。それ以外はトリム長×ピッチ/長さ制御で近似。
std::int64_t OtoMadSamplerProcessor::samplePlayLengthSamples (int note) const noexcept
{
    const auto* sb = activeSample.load();
    if (sb == nullptr || sb->numSamples <= 0)
        return 0;

    // キャッシュ経路（REAPER Shifter Natural/Manual）: 事前レンダ済みバッファ長が正確
    if (useCachePath())
    {
        const int semi = juce::jlimit (otomad::PitchCache::kMin, otomad::PitchCache::kMax,
                                       note - (int) pRootKey->load() + (int) pPitchSemi->load()
                                         + 12 * (int) pOctave->load());
        if (const auto* c = pitchCache.lookup (semi))
            return c->numSamples;
    }

    const double s = pSampleStart->load(), e = pSampleEnd->load();
    double len = std::max (0.0, e - s) * (double) sb->numSamples;   // トリム長（出力SR, 等速）

    const int algo = (int) pAlgorithm->load();
    if (algo == 0)   // Varispeed: ピッチで再生速度が変わる（長さは 1/ratio）
    {
        const double effSemi = pPitchSemi->load() + pPitchCents->load() * 0.01
                             + 12.0 * pOctave->load()
                             + (double) (note - (int) pRootKey->load());
        const double ratio = std::pow (2.0, effSemi / 12.0);
        if (ratio > 1.0e-6) len /= ratio;
    }
    else if ((int) pDurationMode->load() == 2)   // 長さ保持系の Manual: stretch 倍
    {
        len *= (double) pStretch->load();
    }
    return (std::int64_t) len;
}

// Tail: サンプル末尾から固定量（ms/%/Sync）を削った自動オフ位置（発音からのオフセット）。-1=無効。
// 再生長は発音時点で判るのでレイテンシは増えない。
std::int64_t OtoMadSamplerProcessor::computeTailAutoOffOffset (int note) const noexcept
{
    const int mode = (int) pTailMode->load();   // 0=Off,1=%,2=ms,3=Sync
    if (mode <= 0)
        return -1;

    const std::int64_t playLen = samplePlayLengthSamples (note);
    if (playLen <= 0)
        return -1;

    const double sr = hostSampleRate.load();
    std::int64_t cut = 0;
    if (mode == 1)        // % : Tail% は「削る割合」。削る量 = 再生長 × Tail%/100
        cut = (std::int64_t) ((double) playLen * (double) pTailPercent->load() * 0.01);
    else if (mode == 2)   // ms
        cut = (std::int64_t) ((double) pTailMs->load() * 0.001 * sr);
    else                  // Sync（テンポが無ければ ms にフォールバック）
    {
        if (! hostBpmValid || hostBpm <= 0.0)
            cut = (std::int64_t) ((double) pTailMs->load() * 0.001 * sr);
        else
        {
            static constexpr double beatsTable[] = { 4.0/128, 4.0/64, 4.0/32, 4.0/16, 4.0/8, 4.0/4, 4.0/2, 4.0/1 };
            const int idx = juce::jlimit (0, 7, (int) pTailSyncDiv->load());
            cut = (std::int64_t) (beatsTable[idx] * (60.0 / hostBpm) * sr);
        }
    }
    if (cut <= 0)
        return -1;

    // Release を考慮: リリースは自動オフ後に伸びるので、その分だけ早めにオフして
    // リリースが「削り位置」でちょうど終わるようにする（削る量＝実際の発音終端に一致）。
    const std::int64_t rel = (std::int64_t) ((double) pRelease->load() * 0.001 * sr);

    // 5ms 下限は撤去（とりあえず）。削りすぎて 0 以下になったら発音開始で即オフ。
    const std::int64_t off = playLen - cut - rel;
    return off > 0 ? off : 0;
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

void OtoMadSamplerProcessor::handleMidiMessage (const juce::MidiMessage& msg, std::int64_t absSample) noexcept
{
    if (msg.isNoteOn())
    {
        const int   note = msg.getNoteNumber();
        const float vel  = msg.getFloatVelocity();

        // Tail: サンプル末尾から固定量を削った位置で自動オフを予約。-1なら無効。
        if (note >= 0 && note < kNumNotes)
        {
            const std::int64_t off = computeTailAutoOffOffset (note);
            pendingOff[(std::size_t) note] = off >= 0 ? absSample + off : -1;
        }
        const float s = pSampleStart->load(), e = pSampleEnd->load();
        const bool  snap = pSnap->load() > 0.5f;

        if (useCachePath())
        {
            const int semi = juce::jlimit (otomad::PitchCache::kMin, otomad::PitchCache::kMax,
                                           note - (int) pRootKey->load() + (int) pPitchSemi->load()
                                             + 12 * (int) pOctave->load());
            if (const auto* cached = pitchCache.lookup (semi))
                // キャッシュはトリム範囲だけをレンダ済み → 全体(0..1)を再生（二重トリム防止）
                voices.noteOn (note, vel, cached, 0.0f, 1.0f, snap, true, (float) semi);
            else
            {
                pitchCache.request (semi);                                           // 背景でレンダリング要求
                voices.noteOn (note, vel, activeSample.load(), s, e, snap, true, 0.0f); // 一発目は Varispeed で綺麗に（原音をトリム）
            }
        }
        else
        {
            voices.noteOn (note, vel, activeSample.load(), s, e, snap);
        }
    }
    else if (msg.isNoteOff())
    {
        const int note = msg.getNoteNumber();
        if (note >= 0 && note < kNumNotes)
            pendingOff[(std::size_t) note] = -1;   // 実際の離鍵が来たら自動オフ予約は取り消し
        voices.noteOff (note);
    }
    else if (msg.isPitchWheel())
    {
        const float norm = ((float) msg.getPitchWheelValue() - 8192.0f) / 8192.0f;
        voices.setPitchBendSemi (norm * (float) (int) pBendRange->load());
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        voices.allNotesOff();
        pendingOff.fill (-1);   // 保留中の Tail オフも破棄
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

void OtoMadSamplerProcessor::maybeApplyDefaultReaperMode()
{
    if (reaperDefaultChecked || ! prepared.load() || ! reaperApi.isAvailable())
        return;
    reaperDefaultChecked = true;
    if (stateWasRestored.load())
        return;   // 復元済みインスタンスはユーザ設定を尊重

    // 新規インスタンス: 既定を élastique Soloist に。名前で解決（版によりインデックスが変わるため）。
    const auto modes = getReaperModeNames();
    int soloist = -1;
    for (int i = 0; i < modes.size(); ++i)               // 新しい版(3.x)の Soloist を優先
        if (modes[i].containsIgnoreCase ("soloist") && modes[i].contains ("3.")) { soloist = i; break; }
    if (soloist < 0)
        for (int i = 0; i < modes.size(); ++i)           // 無ければ任意の Soloist
            if (modes[i].containsIgnoreCase ("soloist")) { soloist = i; break; }
    if (soloist < 0)
        return;   // Soloist が見つからないホスト/版では既定のまま

    int sub = 0;                                         // サブモードは Monophonic 優先
    const auto subs = getReaperSubModeNames (soloist);
    for (int i = 0; i < subs.size(); ++i)
        if (subs[i].containsIgnoreCase ("mono")) { sub = i; break; }

    if (auto* pm = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (otomad::params::reaperMode)))
        *pm = soloist;
    if (auto* ps = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (otomad::params::reaperSubMode)))
        *ps = sub;
}

void OtoMadSamplerProcessor::serviceCache()
{
    maybeApplyDefaultReaperMode();   // 新規インスタンスの初期モード（Soloist）を一度だけ適用

    // Manual のときは stretchAmount から timeRatio を算出（Natural は 1.0）
    double timeRatio = 1.0;
    if ((int) pDurationMode->load() == 2)
    {
        const float st = pStretch->load();
        timeRatio = st > 0.0f ? 1.0 / (double) st : 1.0;
    }

    // 素材/モード/フォルマント/ストレッチ/トリム を反映（変わっていれば ready を無効化して作り直す）
    const bool changed = pitchCache.configure (activeSample.load(), sampleVersion.load(),
                          (int) pReaperMode->load(), (int) pReaperSubMode->load(),
                          hostSampleRate.load(), pFormant->load(), timeRatio,
                          pSampleStart->load(), pSampleEnd->load());

    // 設定確定時にプリウォーム: 現在の pitchSemi を中心に ±48 半音（全域）をまとめて背景生成（停止中に貯める）
    if (changed && useCachePath())
    {
        const int c = (int) pPitchSemi->load() + 12 * (int) pOctave->load();
        pitchCache.requestRange (c - 48, c + 48);
    }

    // 保留中の音程があれば背景スレッドで並列レンダリング。cacheThreads 本まで稼働数を補充する。
    // 各ジョブは renderPending を空になるまで回し、完了したら稼働数を減らす。
    if (pitchCache.hasPending())
    {
        while (cacheJobsActive.load() < cacheThreads)
        {
            cacheJobsActive.fetch_add (1);
            loadPool.addJob ([this]
            {
                while (pitchCache.renderPending()) {}
                cacheJobsActive.fetch_sub (1);
            });
        }
    }
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

// 1窓の YIN。x は DC除去済みのモノ窓。outFreq に基本周波数、戻り値は非周期性(0=完全周期, 小さいほど信頼)。
static double yinDetectWindow (const float* x, int W, double sr, double& outFreq) noexcept
{
    outFreq = 0.0;
    const int minTau = std::max (2, (int) (sr / 1500.0));   // 上限 ~1500Hz
    const int maxTau = std::min (W / 2, (int) (sr / 50.0));  // 下限 ~50Hz
    if (maxTau <= minTau)
        return 1.0;

    std::vector<float> dn ((std::size_t) (maxTau + 1), 1.0f);   // 累積平均正規化差分
    float running = 0.0f;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j + tau < W; ++j)
        {
            const float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        running += sum;
        dn[(std::size_t) tau] = running > 0.0f ? sum * (float) (tau - minTau + 1) / running : 1.0f;
    }

    // 標準YIN: 閾値を割る最初の谷まで下って、その谷の底(局所最小)を採る（オクターブ下取りを防ぐ）。
    const float thr = 0.15f;
    int best = -1;
    for (int tau = minTau; tau < maxTau; ++tau)
    {
        if (dn[(std::size_t) tau] < thr)
        {
            while (tau + 1 < maxTau && dn[(std::size_t) (tau + 1)] < dn[(std::size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)   // 閾値未達 → 全体最小
    {
        int mt = minTau; float mv = dn[(std::size_t) minTau];
        for (int tau = minTau + 1; tau <= maxTau; ++tau)
            if (dn[(std::size_t) tau] < mv) { mv = dn[(std::size_t) tau]; mt = tau; }
        best = mt;
    }

    // 放物線補間
    double tauR = best;
    if (best > minTau && best < maxTau)
    {
        const float a = dn[(std::size_t) (best - 1)], b = dn[(std::size_t) best], c = dn[(std::size_t) (best + 1)];
        const float den = a + c - 2.0f * b;
        if (std::abs (den) > 1.0e-9f)
            tauR = best + 0.5 * (double) ((a - c) / den);
    }
    if (tauR <= 0.0)
        return 1.0;
    outFreq = sr / tauR;
    return dn[(std::size_t) best];
}

bool OtoMadSamplerProcessor::detectAndSetRoot()
{
    const auto* sb = activeSample.load();
    if (sb == nullptr || sb->numSamples <= 0 || sb->numChannels <= 0)
        return false;

    const double sr = sb->sampleRate > 0.0 ? sb->sampleRate : hostSampleRate.load();
    const std::int64_t total = sb->numSamples;
    std::int64_t s = (std::int64_t) std::llround ((double) juce::jlimit (0.0f, 1.0f, pSampleStart->load()) * (double) total);
    std::int64_t e = (std::int64_t) std::llround ((double) juce::jlimit (0.0f, 1.0f, pSampleEnd->load())   * (double) total);
    s = juce::jlimit<std::int64_t> (0, total, s);
    e = juce::jlimit<std::int64_t> (0, total, e);

    // アタックを少し飛ばした解析領域
    const std::int64_t skip   = std::min<std::int64_t> ((e - s) / 20, (std::int64_t) (0.01 * sr));
    const std::int64_t rStart = s + skip;
    const std::int64_t rEnd   = e;
    if (rEnd - rStart < 1024)
        return false;

    // 領域全体に複数窓を敷いて、各窓のYIN結果(信頼できるもの)の音程を中央値で採る。
    // → 単一窓のオクターブ誤検出・トランジェント・ノイズに強くする。
    const int W = (int) std::min<std::int64_t> (4096, rEnd - rStart);
    const float inv = 1.0f / (float) sb->numChannels;
    const int maxWin = 24;
    std::int64_t hop = (rEnd - rStart - W) / std::max (1, maxWin - 1);
    if (hop < W / 4) hop = std::max<std::int64_t> (W / 4, 1);   // 領域が短ければ窓は少数

    std::vector<float> buf ((std::size_t) W);
    std::vector<float> notes;      // 信頼できた窓の音程（MIDI, float）
    notes.reserve (32);

    for (std::int64_t ws = rStart; ws + 1024 <= rEnd; ws += hop)
    {
        const int w = (int) std::min<std::int64_t> (W, rEnd - ws);
        if (w < 1024)
            break;

        // モノ化 ＋ DC除去 ＋ 実効レベルチェック（無音窓は捨てる）
        double mean = 0.0, energy = 0.0;
        for (int i = 0; i < w; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < sb->numChannels; ++ch)
                m += sb->data[(std::size_t) ch][(std::size_t) (ws + i)];
            m *= inv;
            buf[(std::size_t) i] = m;
            mean += m;
        }
        mean /= (double) w;
        for (int i = 0; i < w; ++i) { buf[(std::size_t) i] -= (float) mean; energy += (double) buf[(std::size_t) i] * buf[(std::size_t) i]; }
        if (energy / (double) w < 1.0e-6)   // ほぼ無音 → スキップ
            continue;

        double freq = 0.0;
        const double aper = yinDetectWindow (buf.data(), w, sr, freq);
        if (aper < 0.2 && freq > 0.0)
            notes.push_back ((float) (69.0 + 12.0 * std::log2 (freq / 440.0)));

        if ((int) notes.size() >= maxWin)
            break;
    }

    if (notes.size() < 3)   // 信頼できる窓が少なすぎ → 明確な音程なしと判断
        return false;

    std::sort (notes.begin(), notes.end());
    const double P = (double) notes[notes.size() / 2];      // 検出したサンプルの実ピッチ（MIDI）

    // 50セント(=0.5半音)グリッドに合わせる: 実ピッチ P を最寄りのグリッド G に補正する。
    //   Root = P に最も近い鍵盤（そのキーを押すと G が鳴る）
    //   Cent = P→G の補正量（最大 ±25 セント）
    const double G = std::round (P * 2.0) / 2.0;
    const int    root  = juce::jlimit (0, 127, (int) std::lround (P));
    const double cents = juce::jlimit (-100.0, 100.0, (G - P) * 100.0);

    if (auto* pr = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter (otomad::params::rootKey)))
        *pr = root;
    if (auto* pc = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (otomad::params::pitchCents)))
        *pc = (float) cents;
    return true;
}

//==============================================================================
juce::String OtoMadSamplerProcessor::getCurrentVersion()
{
   #ifdef OTOMAD_VERSION
    return juce::String (OTOMAD_VERSION);
   #else
    return "0.0.0";
   #endif
}

juce::URL OtoMadSamplerProcessor::getReleasesUrl()
{
    return juce::URL ("https://github.com/neon-uriel/Sampler81800/releases");
}

juce::String OtoMadSamplerProcessor::getLatestVersion() const
{
    const juce::ScopedLock sl (updateStrLock);
    return latestVersionStr;
}

// "0.2.0" > "0.1.9" などをドット区切り整数で比較（先頭の 'v' は無視）。
static bool versionIsNewer (const juce::String& cand, const juce::String& base)
{
    auto toks = [] (juce::String s)
    {
        juce::StringArray t;
        t.addTokens (s.retainCharacters ("0123456789."), ".", "");
        return t;
    };
    const auto a = toks (cand), b = toks (base);
    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
    {
        const int va = i < a.size() ? a[i].getIntValue() : 0;
        const int vb = i < b.size() ? b[i].getIntValue() : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

void OtoMadSamplerProcessor::checkForUpdatesAsync (bool force)
{
    if (! force && updateCheckStarted.exchange (true))
        return;   // 自動確認は一度だけ
    if (force)
        updateCheckStarted.store (true);

    loadPool.addJob ([this]
    {
        juce::URL url ("https://api.github.com/repos/neon-uriel/Sampler81800/releases/latest");
        const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs (5000)
                            .withExtraHeaders ("User-Agent: OtoMadSampler\nAccept: application/vnd.github+json");

        std::unique_ptr<juce::InputStream> stream (url.createInputStream (opts));
        if (stream == nullptr)
            return;

        const auto text = stream->readEntireStreamAsString();
        const auto json = juce::JSON::parse (text);
        const auto tag  = json.getProperty ("tag_name", juce::var()).toString();
        if (tag.isEmpty())
            return;

        {
            const juce::ScopedLock sl (updateStrLock);
            latestVersionStr = tag.retainCharacters ("0123456789.");
        }
        updateAvailable.store (versionIsNewer (tag, getCurrentVersion()));
    });
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

void OtoMadSamplerProcessor::setBackgroundImageFromFile (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return;
    juce::Image img = juce::ImageFileFormat::loadFrom (in);
    if (! img.isValid())
        return;

    // 保存用に PNG へ再エンコード（形式非依存で state に埋め込める）
    juce::MemoryBlock png;
    {
        juce::MemoryOutputStream os (png, false);
        juce::PNGImageFormat fmt;
        if (! fmt.writeImageToStream (img, os))
            return;
    }
    bgImage = img;
    bgPng   = png;
    appearanceVersion.fetch_add (1);
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
        se->setAttribute ("normGain", (double) normGain.load());   // ノーマライズ倍率を保存

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

    // 外観設定（メインカラー / 背景透過率 / 背景画像PNG）
    writeAppearance (*root->createNewChildElement ("appearance"));

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
    {
        apvts.replaceState (juce::ValueTree::fromXml (*apvtsXml));
        stateWasRestored.store (true);   // 復元済み → 初期モード自動設定はしない
    }

    if (sampleXml != nullptr)
    {
        restoreSample (*sampleXml);
        // ノーマライズ倍率を復元（restoreSample→publishSample が 1.0 に戻すので後で上書き）
        normGain.store ((float) sampleXml->getDoubleAttribute ("normGain", 1.0));
    }

    // 外観設定の復元（state に無ければコンストラクタで読んだ共通既定のまま）
    if (auto* ae = xml->getChildByName ("appearance"))
        readAppearance (*ae);
}

//==============================================================================
juce::File OtoMadSamplerProcessor::defaultAppearanceFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("OtoMadSampler").getChildFile ("appearance.xml");
}

void OtoMadSamplerProcessor::writeAppearance (juce::XmlElement& ae) const
{
    ae.setAttribute ("mainColour", juce::String::toHexString ((int) mainColour.load()));
    ae.setAttribute ("bgOpacity", (double) bgOpacity.load());
    if (bgPng.getSize() > 0)
        ae.addTextElement (bgPng.toBase64Encoding());
}

void OtoMadSamplerProcessor::readAppearance (const juce::XmlElement& ae)
{
    if (ae.hasAttribute ("mainColour"))
        mainColour.store ((juce::uint32) ae.getStringAttribute ("mainColour").getHexValue64());
    bgOpacity.store (juce::jlimit (0.0f, 1.0f, (float) ae.getDoubleAttribute ("bgOpacity", (double) bgOpacity.load())));

    const auto b64 = ae.getAllSubText().trim();
    bgImage = juce::Image();
    bgPng.reset();
    if (b64.isNotEmpty() && bgPng.fromBase64Encoding (b64) && bgPng.getSize() > 0)
        bgImage = juce::ImageFileFormat::loadFrom (bgPng.getData(), bgPng.getSize());

    appearanceVersion.fetch_add (1);
}

void OtoMadSamplerProcessor::loadDefaultAppearance()
{
    const auto f = defaultAppearanceFile();
    if (! f.existsAsFile())
        return;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("appearance"))
            readAppearance (*xml);
}

void OtoMadSamplerProcessor::applyBroadcastAppearance (juce::uint32 argb, float opacity,
                                                       const juce::MemoryBlock& png)
{
    mainColour.store (argb);
    bgOpacity.store (juce::jlimit (0.0f, 1.0f, opacity));
    bgPng   = png;
    bgImage = png.getSize() > 0 ? juce::ImageFileFormat::loadFrom (png.getData(), png.getSize())
                                : juce::Image();
    appearanceVersion.fetch_add (1);   // 各インスタンスのエディタがタイマで拾って再描画する
}

void OtoMadSamplerProcessor::saveAppearanceAsDefault()
{
    juce::XmlElement ae ("appearance");
    writeAppearance (ae);
    const auto f = defaultAppearanceFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (ae.toString());

    // 即時ブロードキャスト: 同一プロセス内の全インスタンスへ現在の外観を反映
    const auto argb = mainColour.load();
    const auto op   = bgOpacity.load();
    auto& hub = AppearanceHub::get();
    std::lock_guard<std::mutex> lk (hub.m);
    for (auto* inst : hub.instances)
        if (inst != this)
            inst->applyBroadcastAppearance (argb, op, bgPng);
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
