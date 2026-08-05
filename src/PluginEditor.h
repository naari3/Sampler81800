#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "core/Params.h"
#include "gui/WaveformView.h"
#include "gui/DropZone.h"

//==============================================================================
// ポルタメントカーブの読み取り専用表示（§6.4）。portaCurve に連動して s(p)=p^(4^-curve) を描く。
class CurveDisplay : public juce::Component,
                     private juce::Timer
{
public:
    explicit CurveDisplay (OtoMadSamplerProcessor& p) : processor (p) { startTimerHz (20); }
    ~CurveDisplay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0f0f13));
        g.fillRoundedRectangle (b, 3.0f);

        const float curve = processor.getAPVTS().getRawParameterValue (otomad::params::portaCurve)->load();
        const float k = std::pow (4.0f, -curve);

        juce::Path p;
        constexpr int N = 48;
        for (int i = 0; i <= N; ++i)
        {
            const float x = (float) i / (float) N;
            const float y = std::pow (x, k);
            const float px = b.getX() + x * b.getWidth();
            const float py = b.getBottom() - y * b.getHeight();
            if (i == 0) p.startNewSubPath (px, py);
            else        p.lineTo (px, py);
        }
        g.setColour (juce::Colour (processor.getMainColourARGB()));
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

private:
    void timerCallback() override { repaint(); }
    OtoMadSamplerProcessor& processor;
};

//==============================================================================
// ADSR エンベロープの可視化（読み取り専用）。attack/decay/sustain/release に連動。
class AdsrDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit AdsrDisplay (OtoMadSamplerProcessor& p) : processor (p) { startTimerHz (20); }
    ~AdsrDisplay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0f0f13));
        g.fillRoundedRectangle (b, 3.0f);

        auto& ap = processor.getAPVTS();
        const float a = ap.getRawParameterValue (otomad::params::attack)->load();
        const float d = ap.getRawParameterValue (otomad::params::decay)->load();
        const float s = ap.getRawParameterValue (otomad::params::sustain)->load();
        const float r = ap.getRawParameterValue (otomad::params::release)->load();

        // 時間(ms)は sqrt で圧縮して短時間も見えるように。サステインは固定幅で表現。
        auto sc = [] (float ms) { return std::sqrt (std::max (0.0f, ms)); };
        const float wa = sc (a), wd = sc (d), wr = sc (r);
        const float sum   = std::max (1.0e-3f, wa + wd + wr);
        const float sFrac = 0.22f;
        const float avail = b.getWidth() * (1.0f - sFrac);
        const float xa = avail * (wa / sum), xd = avail * (wd / sum), xr = avail * (wr / sum);
        const float xs = b.getWidth() * sFrac;

        const float yTop = b.getY() + 3.0f, yBot = b.getBottom() - 3.0f;
        const float sy   = yBot - (yBot - yTop) * juce::jlimit (0.0f, 1.0f, s);

        juce::Path p;
        float x = b.getX();
        p.startNewSubPath (x, yBot);
        p.lineTo (x += xa, yTop);     // Attack: 0 → peak
        p.lineTo (x += xd, sy);       // Decay:  peak → sustain
        p.lineTo (x += xs, sy);       // Sustain 保持
        p.lineTo (x += xr, yBot);     // Release: → 0

        const juce::Colour c (processor.getMainColourARGB());
        juce::Path fill (p);
        fill.lineTo (x, yBot); fill.closeSubPath();
        g.setColour (c.withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

private:
    void timerCallback() override { repaint(); }
    OtoMadSamplerProcessor& processor;
};

//==============================================================================
// 設定オーバーレイ: 背景画像 / 背景の不透明度 / メインカラー(RGB) を編集する。
class SettingsOverlay : public juce::Component
{
public:
    explicit SettingsOverlay (OtoMadSamplerProcessor&);

    std::function<void()> onChanged;    // 色/画像/不透明度が変わったらエディタへ通知
    void refreshFromProcessor();        // 現在値をコントロールへ反映

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void pushColour();                  // R/G/B スライダ → processor

    OtoMadSamplerProcessor& processor;

    juce::Label      title;
    juce::TextButton chooseBg, clearBg, saveDefault, closeBtn;
    juce::Label      opacityLbl;  juce::Slider opacity;
    juce::Label      colourLbl;   juce::Slider rSl, gSl, bSl;
    juce::Rectangle<int> swatchBounds;   // メインカラーのプレビュー矩形
    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;        // refresh 中はスライダの onValueChange を無視

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsOverlay)
};

//==============================================================================
class OtoMadSamplerEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit OtoMadSamplerEditor (OtoMadSamplerProcessor&);
    ~OtoMadSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyMainColour();   // メインカラーを LookAndFeel と各コンポーネントに反映

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttach> attach;
    };
    Knob& addKnob (const juce::String& paramID, const juce::String& text,
                   juce::Slider::SliderStyle style);

    struct Combo
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<ComboAttach> attach;
    };
    Combo& addCombo (const juce::String& paramID, const juce::String& text,
                     const juce::StringArray& items);

    OtoMadSamplerProcessor& processor;

    otomad::gui::WaveViewState  waveView;   // 波形ズーム窓（waveform/dropZone より前に宣言）
    otomad::gui::WaveformView   waveform;
    otomad::gui::DropZone       dropZone;
    juce::MidiKeyboardComponent keyboard;
    CurveDisplay                curveDisplay;
    AdsrDisplay                 adsrDisplay;
    juce::TooltipWindow         tooltipWindow { this, 500 };   // ノブ等のホバーヘルプ

    std::vector<std::unique_ptr<Knob>>  knobs;
    std::vector<std::unique_ptr<Combo>> combos;

    juce::ToggleButton snapButton { "Snap ZC" };
    std::unique_ptr<ButtonAttach> snapAttach;
    juce::ToggleButton phaseLockButton { "Phase Lock" };
    std::unique_ptr<ButtonAttach> phaseLockAttach;
    juce::TextButton normalizeButton { "Normalize" };

    Knob* kSemi = nullptr; Knob* kCents = nullptr; Knob* kRoot = nullptr; Knob* kGain = nullptr;
    Knob* kAtk = nullptr;  Knob* kDec = nullptr;   Knob* kSus = nullptr;  Knob* kRel = nullptr;
    Knob* kStart = nullptr; Knob* kEnd = nullptr;
    Knob* kPTime = nullptr; Knob* kPCurve = nullptr; Knob* kGroup = nullptr;
    Knob* kMaxV = nullptr;  Knob* kBend = nullptr;
    Knob* kStretch = nullptr; Knob* kFormant = nullptr;
    Knob* kTailPct = nullptr; Knob* kTailMs = nullptr;
    Combo* cInterp = nullptr; Combo* cPMode = nullptr; Combo* cPShape = nullptr; Combo* cPoly = nullptr;
    Combo* cAlgo = nullptr; Combo* cDur = nullptr; Combo* cSync = nullptr;
    Combo* cTailMode = nullptr; Combo* cTailSync = nullptr;

    // REAPER モード/サブモードは動的な名前リストなので手動管理のコンボにする
    juce::ComboBox rModeBox, rSubBox;
    juce::Label    rModeLbl, rSubLbl;
    bool modePopulated = false;
    int  subPopulatedForMode = -1;
    void setIntParam (const juce::String& id, int value);

    int lastReaperMode = -1, lastReaperSubMode = -1;
    juce::Label statusLabel;
    juce::Label reaperModeLabel;
    juce::Image logo;

    // ピッチキャッシュ生成の進捗バー（ヘッダ右側, 生成中のみ表示）
    double            cacheProgressValue = 0.0;
    juce::ProgressBar cacheBar { cacheProgressValue };

    // 外観: メインカラー用 LookAndFeel と設定オーバーレイ
    juce::LookAndFeel_V4 lnf;
    juce::TextButton     settingsButton;
    SettingsOverlay      settingsOverlay { processor };
    int                  lastAppearanceVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerEditor)
};
