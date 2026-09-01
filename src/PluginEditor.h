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
// ポルタメントカーブ表示＋編集。上下ドラッグで portaCurve(-1..1) を変える。s(p)=p^(4^-curve)。
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

    void mouseDown (const juce::MouseEvent& e) override
    { if (auto* p = processor.getAPVTS().getParameter (otomad::params::portaCurve)) p->beginChangeGesture(); drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp   (const juce::MouseEvent&)  override
    { if (auto* p = processor.getAPVTS().getParameter (otomad::params::portaCurve)) p->endChangeGesture(); }

private:
    void drag (const juce::MouseEvent& e)
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.y - b.getY()) / juce::jmax (1.0f, b.getHeight()));
        const float curve = juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * t);   // 上=+1 / 下=-1
        if (auto* p = processor.getAPVTS().getParameter (otomad::params::portaCurve))
            p->setValueNotifyingHost (p->convertTo0to1 (curve));
    }
    void timerCallback() override { repaint(); }
    OtoMadSamplerProcessor& processor;
};

//==============================================================================
// ADSR エンベロープ表示＋編集。縦線でブレークポイントを示し、点をドラッグして A/D/S/R を変える。
class AdsrDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit AdsrDisplay (OtoMadSamplerProcessor& p) : processor (p) { startTimerHz (30); }
    ~AdsrDisplay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0f0f13));
        g.fillRoundedRectangle (b, 3.0f);

        float xa, xd, xs, xr, yTop, yBot, sy;
        computeLayout (b, xa, xd, xs, xr, yTop, yBot, sy);
        const float x0 = b.getX();
        const float ax = x0 + xa, dx2 = ax + xd, sx = dx2 + xs, rx = sx + xr;
        const juce::Colour c (processor.getMainColourARGB());

        // 縦の区切り線（各ブレークポイント）
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        for (float lx : { ax, dx2, sx })
            g.drawVerticalLine (juce::roundToInt (lx), b.getY(), b.getBottom());

        juce::Path p;
        p.startNewSubPath (x0, yBot);
        p.lineTo (ax, yTop); p.lineTo (dx2, sy); p.lineTo (sx, sy); p.lineTo (rx, yBot);
        juce::Path fill (p); fill.lineTo (rx, yBot); fill.closeSubPath();
        g.setColour (c.withAlpha (0.15f)); g.fillPath (fill);
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (2.0f));

        // ドラッグ点
        for (auto pt : { juce::Point<float> (ax, yTop), juce::Point<float> (dx2, sy), juce::Point<float> (rx, yBot) })
            g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        float xa, xd, xs, xr, yTop, yBot, sy; computeLayout (b, xa, xd, xs, xr, yTop, yBot, sy);
        const float x0 = b.getX(); const float ax = x0 + xa, dx2 = ax + xd, rx = dx2 + xs + xr;
        const juce::Point<float> hA (ax, yTop), hD (dx2, sy), hR (rx, yBot);
        auto dist = [&] (juce::Point<float> h) { return std::hypot (h.x - e.position.x, h.y - e.position.y); };
        const float dA = dist (hA), dD = dist (hD), dR = dist (hR);
        grab = (dA <= dD && dA <= dR) ? 0 : (dD <= dR ? 1 : 2);
        lastPos = e.position;
        beginGestures();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const float dx = e.position.x - lastPos.x, dy = e.position.y - lastPos.y;
        lastPos = e.position;
        const float kSqrt = 70.7107f / juce::jmax (1.0f, b.getWidth());   // sqrt(5000)/幅
        auto adjMs = [&] (const char* id, float dpx)
        {
            const float sq = std::sqrt (std::max (0.0f, get (id))) + dpx * kSqrt;
            setP (id, juce::jlimit (0.0f, 5000.0f, sq * sq));
        };
        if (grab == 0) adjMs (otomad::params::attack, dx);
        else if (grab == 1)
        {
            adjMs (otomad::params::decay, dx);
            setP (otomad::params::sustain, juce::jlimit (0.0f, 1.0f,
                    get (otomad::params::sustain) - dy / juce::jmax (1.0f, b.getHeight())));
        }
        else adjMs (otomad::params::release, dx);
    }
    void mouseUp (const juce::MouseEvent&) override { endGestures(); grab = -1; }

private:
    void computeLayout (juce::Rectangle<float> b, float& xa, float& xd, float& xs, float& xr,
                        float& yTop, float& yBot, float& sy)
    {
        const float a = get (otomad::params::attack), d = get (otomad::params::decay);
        const float s = get (otomad::params::sustain), r = get (otomad::params::release);
        auto sc = [] (float ms) { return std::sqrt (std::max (0.0f, ms)); };
        const float wa = sc (a), wd = sc (d), wr = sc (r);
        const float sum = std::max (1.0e-3f, wa + wd + wr);
        const float sFrac = 0.22f;
        const float avail = b.getWidth() * (1.0f - sFrac);
        xa = avail * (wa / sum); xd = avail * (wd / sum); xr = avail * (wr / sum); xs = b.getWidth() * sFrac;
        yTop = b.getY() + 3.0f; yBot = b.getBottom() - 3.0f;
        sy = yBot - (yBot - yTop) * juce::jlimit (0.0f, 1.0f, s);
    }
    float get (const char* id) const { return processor.getAPVTS().getRawParameterValue (id)->load(); }
    void  setP (const char* id, float v)
    { if (auto* p = processor.getAPVTS().getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    void beginGestures() { for (auto id : { otomad::params::attack, otomad::params::decay, otomad::params::sustain, otomad::params::release }) if (auto* p = processor.getAPVTS().getParameter (id)) p->beginChangeGesture(); }
    void endGestures()   { for (auto id : { otomad::params::attack, otomad::params::decay, otomad::params::sustain, otomad::params::release }) if (auto* p = processor.getAPVTS().getParameter (id)) p->endChangeGesture(); }
    void timerCallback() override { repaint(); }

    OtoMadSamplerProcessor& processor;
    int grab = -1;
    juce::Point<float> lastPos;
};

//==============================================================================
// Root 鍵を色付け表示し、右クリックで Root を設定できる鍵盤。
class RootKeyboard : public juce::MidiKeyboardComponent
{
public:
    RootKeyboard (juce::MidiKeyboardState& s, Orientation o) : juce::MidiKeyboardComponent (s, o) {}

    std::function<void (int)> onSetRoot;
    juce::Colour markerColour { juce::Colours::red };
    void setRootNote (int n) { if (n != rootNote) { rootNote = n; repaint(); } }

protected:
    bool mouseDownOnKey (int note, const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())   // 右クリックした鍵を Root に。音は鳴らさない。
        {
            if (onSetRoot) onSetRoot (note);
            return false;
        }
        return true;
    }
    void drawWhiteNote (int n, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote (n, g, area, isDown, isOver, lineColour, textColour);
        if (n == rootNote) { g.setColour (markerColour.withAlpha (0.45f)); g.fillRect (area); }
    }
    void drawBlackNote (int n, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override
    {
        juce::MidiKeyboardComponent::drawBlackNote (n, g, area, isDown, isOver, noteFillColour);
        if (n == rootNote) { g.setColour (markerColour.withAlpha (0.6f)); g.fillRect (area); }
    }

private:
    int rootNote = 60;
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
// UI スケール用のコンテンツ層。全コントロールをここに入れ、これを transform で拡大縮小する
// （エディタ自身の transform はホスト用に予約されているため, JUCE の作法）。
struct ScaledContent : juce::Component
{
    std::function<void (juce::Graphics&)> onPaint;
    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
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

    static constexpr int kBaseW = 860, kBaseH = 772;   // 基準(100%)サイズ
    static constexpr int kNumScales = 6;
    static constexpr float kScales[kNumScales] = { 50.0f, 75.0f, 100.0f, 125.0f, 150.0f, 200.0f };
    float uiScale = 1.0f;
    ScaledContent  content;                            // 全コントロールの親（スケール対象）
    juce::ComboBox scaleBox;                           // 50/100/125/150/200%
    void applyUiScale (float pct);
    void layoutContent();                              // content 内を基準サイズでレイアウト
    void paintContent (juce::Graphics&);               // 背景・ロゴ（基準座標で描画）

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
    RootKeyboard                keyboard;
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
    juce::TextButton detectButton { "Detect" };   // 基音を検出して Root に設定

    Knob* kSemi = nullptr; Knob* kCents = nullptr; Knob* kOct = nullptr; Knob* kRoot = nullptr; Knob* kGain = nullptr;
    Knob* kAtk = nullptr;  Knob* kDec = nullptr;   Knob* kSus = nullptr;  Knob* kRel = nullptr;
    Knob* kStart = nullptr; Knob* kEnd = nullptr;
    Knob* kPTime = nullptr; Knob* kPCurve = nullptr; Knob* kGroup = nullptr;
    Knob* kMaxV = nullptr;  Knob* kBend = nullptr;
    Knob* kStretch = nullptr;   // FORMANT は UI から外したのでノブは持たない
    Combo* cInterp = nullptr; Combo* cPMode = nullptr; Combo* cPShape = nullptr; Combo* cPoly = nullptr;
    Combo* cAlgo = nullptr; Combo* cDur = nullptr; Combo* cSync = nullptr;

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
    juce::TextButton     updateButton;   // 更新あり時のみ表示（クリックでリリースページ）
    SettingsOverlay      settingsOverlay { processor };
    int                  lastAppearanceVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerEditor)
};
