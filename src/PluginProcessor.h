#pragma once

#include <array>
#include <atomic>
#include <cstdint>
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
                               public juce::VST3ClientExtensions,
                               private juce::Timer
{
public:
    OtoMadSamplerProcessor();
    ~OtoMadSamplerProcessor() override;

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

    // 複数サンプルの保持と切り替え（メッセージスレッド）。差し替えて聴き比べるための機能。
    int          getSampleCount() const noexcept { return (int) sampleList.size(); }
    juce::String getSampleName (int index) const;
    int          getActiveIndex() const noexcept { return activeIndex.load(); }
    void         selectSample (int index);
    void         removeSample (int index);
    int  getSampleVersion() const noexcept { return sampleVersion.load(); }

    //==========================================================================
    // 外観設定（GUIスレッド専用。audioスレッドは触らない）。状態に一緒に保存する。
    juce::uint32 getMainColourARGB() const noexcept { return mainColour.load(); }
    void  setMainColourARGB (juce::uint32 argb) noexcept { mainColour.store (argb); appearanceVersion.fetch_add (1); }
    float getBgOpacity() const noexcept { return bgOpacity.load(); }
    void  setBgOpacity (float v) noexcept { bgOpacity.store (juce::jlimit (0.0f, 1.0f, v)); appearanceVersion.fetch_add (1); }
    // パネルの不透明度（背景画像を透かすため）。0.1..1
    float getPanelOpacity() const noexcept { return panelOpacity.load(); }
    void  setPanelOpacity (float v) noexcept { panelOpacity.store (juce::jlimit (0.1f, 1.0f, v)); appearanceVersion.fetch_add (1); }
    const juce::Image& getBackgroundImage() const noexcept { return bgImage; }
    void  setBackgroundImageFromFile (const juce::File& file);
    // Web UI 用: バイト列から背景画像を設定 / 保存済み PNG を取り出す（data URL 化して JS へ渡す）
    void  setBackgroundImageFromMemory (const void* data, std::size_t size);
    const juce::MemoryBlock& getBackgroundPng() const noexcept { return bgPng; }
    void  clearBackgroundImage() noexcept { bgImage = juce::Image(); bgPng.reset(); appearanceVersion.fetch_add (1); }
    int   getAppearanceVersion() const noexcept { return appearanceVersion.load(); }   // 変更検知用

    // 現在の外観を「全インスタンス共通の既定」としてユーザ設定に保存する（メッセージスレッド）。
    // 以後の新規インスタンスはこれを初期値として読み込む。
    void  saveAppearanceAsDefault();
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

    // UI スケール（%）。50/100/125/150/200 を想定。state に保存して復元する。
    float getUiScalePct() const noexcept { return uiScalePct.load(); }
    void  setUiScalePct (float pct) noexcept { uiScalePct.store (juce::jlimit (25.0f, 400.0f, pct)); }

    // エディタのサイズ（0 なら既定を使う）。state に保存して次回も同じ大きさで開く。
    int  getEditorW() const noexcept { return editorW.load(); }
    int  getEditorH() const noexcept { return editorH.load(); }
    void setEditorSize (int w, int h) noexcept { editorW.store (w); editorH.store (h); }

    // GUI表示用: ピッチキャッシュ生成の進捗（0..1）と、生成中かどうか。
    // キャッシュ経路でないときは常に完了(1.0)/非ビジー扱い。
    bool  isCacheBusy() const noexcept
    { return useCachePath() && pitchCache.pendingCount() > 0; }
    float getCacheProgress() const noexcept
    {
        if (! useCachePath()) return 1.0f;
        const int r = pitchCache.readyCount();
        const int tot = r + pitchCache.pendingCount();
        return tot > 0 ? (float) r / (float) tot : 1.0f;
    }

    // ピッチ検出: 現在のサンプル(トリム範囲)の基音を解析して Root を自動設定（メッセージスレッド）。
    // 検出できたら true。明確な音程が無ければ Root は変えず false。
    bool  detectAndSetRoot();

    //==========================================================================
    // アップデート確認: GitHub Releases API をバックグラウンドで叩き、最新版と比較する。
    void         checkForUpdatesAsync (bool force = false);
    bool         isUpdateAvailable() const noexcept { return updateAvailable.load(); }
    juce::String getLatestVersion() const;              // 取得できた最新タグ（"0.2.0" 等 / 無ければ空）
    static juce::String getCurrentVersion();            // 自身のバージョン
    static juce::URL    getReleasesUrl();               // リリース一覧ページ

    // ノーマライズ: 現在のサンプルのピークから正規化ゲインを算出（メッセージスレッド）。
    void  normalizeSample();
    void  resetNormalize() noexcept { normGain.store (1.0f); }
    float getNormGain() const noexcept { return normGain.load(); }

    // メッセージスレッドから呼ぶ。バックグラウンドで読み込み、完了後にアトミック公開。
    void loadSampleFromFile (const juce::File& file);

    // Web UI の D&D 用: バイト列から読み込む（WebView では実パスが取得できないため）。
    void loadSampleFromMemory (juce::MemoryBlock bytes, juce::String displayName);

    // REAPERシフタのモード変更を反映（メッセージスレッド, suspend下で再設定＋レイテンシ再報告）。
    void reconfigureReaperMode();

    // ピッチキャッシュの保守（メッセージスレッド, Editorのタイマーから）。
    void serviceCache();

    // コンボボックス用: REAPER のモード名 / サブモード名を列挙（メッセージスレッド）。
    juce::StringArray getReaperModeNames() const;
    juce::StringArray getReaperSubModeNames (int mode) const;

private:
    //==========================================================================
    void timerCallback() override { serviceCache(); }   // UI非依存でキャッシュ駆動

    void renderSlice (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void handleMidiMessage (const juce::MidiMessage& msg) noexcept;
    void updateVoiceParams() noexcept;
    void publishSample (std::shared_ptr<const otomad::SampleBuffer> sb);   // 読み込み完了時に公開
    void addSampleImpl (std::shared_ptr<const otomad::SampleBuffer> sb);   // メッセージスレッドで実処理
    void restoreSample (const juce::XmlElement& sampleXml);                // §3.9 復元

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState            keyboardState;

    juce::AudioFormatManager formatManager;
    // キャッシュのオフラインレンダを並列化するため複数スレッド。各半音は原子的に取り出すので競合しない。
    const int        cacheThreads = juce::jlimit (1, 6, (int) juce::SystemStats::getNumCpus() - 2);
    juce::ThreadPool loadPool { cacheThreads };

    otomad::VoiceManager voices;
    otomad::host::ReaperApi reaperApi;
    otomad::PitchCache      pitchCache;
    std::atomic<int>        cacheJobsActive { 0 };   // 稼働中の背景レンダジョブ数

    // 外観設定（メッセージスレッドで読み書き。bgImage/bgPng はGUI/保存でのみ触る）
    std::atomic<juce::uint32> mainColour { 0xff5cc8ff };   // 既定アクセント色
    std::atomic<float>        bgOpacity  { 0.25f };
    std::atomic<float>        panelOpacity { 1.0f };
    std::atomic<int>          appearanceVersion { 0 };      // 色/画像が変わるたびに +1
    juce::Image               bgImage;                       // 表示用（デコード済み）
    juce::MemoryBlock         bgPng;                         // 保存用（PNGバイト列）

    // 新規インスタンス時に REAPER モード既定を élastique Soloist にする（名前解決・一度だけ）。
    std::atomic<bool> stateWasRestored { false };
    std::atomic<bool> prepared { false };
    bool              reaperDefaultChecked = false;   // メッセージスレッドのみ
    void maybeApplyDefaultReaperMode();

    // 外観の XML 入出力（state と 既定ファイルで共用）
    void writeAppearance (juce::XmlElement& ae) const;
    void readAppearance  (const juce::XmlElement& ae);
    void loadDefaultAppearance();                       // 起動時に既定ファイルを読む
    static juce::File defaultAppearanceFile();

    // 同一プロセス内の他インスタンスからの即時反映（メッセージスレッド）
    void applyBroadcastAppearance (juce::uint32 argb, float opacity, float panelOp,
                                   const juce::MemoryBlock& png);

    // REAPER Shifter で Natural / Manual のときキャッシュ経路（Varispeed再生）を使う。
    // formant / stretch はキャッシュに焼き込むので可。Sync はテンポ依存で動的なので除外。
    bool useCachePath() const noexcept
    {
        if ((int) pAlgorithm->load() != 5) return false;
        const int dur = (int) pDurationMode->load();
        return dur == 0 || dur == 2;   // Natural or Manual
    }

    std::atomic<double> hostSampleRate { 44100.0 };

    // ロックフリー・サンプルスロット。旧バッファは Phase 1 では graveyard で寿命を延ばす
    // （メッセージスレッド専用。Phase 5 で GCスレッド化する）。
    std::atomic<const otomad::SampleBuffer*> activeSample { nullptr };
    std::atomic<int>   sampleVersion { 0 };
    std::atomic<float> normGain { 1.0f };   // ノーマライズ倍率（gain とは別に掛かる）
    std::atomic<float> uiScalePct { 100.0f };   // UI 表示倍率（%）
    std::atomic<int>   editorW { 0 }, editorH { 0 };   // エディタサイズ（0=既定）
    std::vector<std::shared_ptr<const otomad::SampleBuffer>> sampleGraveyard;
    juce::CriticalSection graveyardLock;

    // 読み込み済みサンプルのリスト（メッセージスレッド専用）。切り替えて聴き比べるため複数保持する。
    // ノーマライズ倍率はサンプルごとに持つ。
    // パラメータもスロットごとに保持し、切り替えるとその設定に丸ごと切り替わる。
    std::vector<std::shared_ptr<const otomad::SampleBuffer>> sampleList;
    std::vector<float>          sampleNorm;
    std::vector<juce::ValueTree> sampleParams;
    std::atomic<int>            activeIndex { -1 };

    // RTアクセス用にキャッシュしたパラメータ atomic
    std::atomic<float>* pPitchSemi   = nullptr;
    std::atomic<float>* pPitchCents  = nullptr;
    std::atomic<float>* pOctave      = nullptr;
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
    std::atomic<float>* pVibDepth      = nullptr;
    std::atomic<float>* pVibRate       = nullptr;
    std::atomic<float>* pVibDelay      = nullptr;
    std::atomic<float>* pVibFade       = nullptr;

    double hostBpm = 120.0;
    bool   hostBpmValid = false;
    int    lastReportedLatency = -1;

    // アップデート確認の結果（バックグラウンドで書き, GUIから読む）
    std::atomic<bool> updateCheckStarted { false };
    std::atomic<bool> updateAvailable    { false };
    juce::String          latestVersionStr;
    juce::CriticalSection updateStrLock;

    // 背景スレッドから callAsync でメッセージスレッドへ渡すため（破棄後の実行を防ぐ）
    JUCE_DECLARE_WEAK_REFERENCEABLE (OtoMadSamplerProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerProcessor)
};
