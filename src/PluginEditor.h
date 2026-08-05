#pragma once

#include <cmath>
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
    explicit CurveDisplay (juce::AudioProcessorValueTreeState& s) : apvts (s) { startTimerHz (20); }
    ~CurveDisplay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0f0f13));
        g.fillRoundedRectangle (b, 3.0f);

        const float curve = apvts.getRawParameterValue (otomad::params::portaCurve)->load();
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
        g.setColour (juce::Colour (0xff5cc8ff));
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

private:
    void timerCallback() override { repaint(); }
    juce::AudioProcessorValueTreeState& apvts;
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

    otomad::gui::WaveformView   waveform;
    otomad::gui::DropZone       dropZone;
    juce::MidiKeyboardComponent keyboard;
    CurveDisplay                curveDisplay;

    std::vector<std::unique_ptr<Knob>>  knobs;
    std::vector<std::unique_ptr<Combo>> combos;

    juce::ToggleButton snapButton { "Snap ZC" };
    std::unique_ptr<ButtonAttach> snapAttach;

    Knob* kSemi = nullptr; Knob* kCents = nullptr; Knob* kRoot = nullptr; Knob* kGain = nullptr;
    Knob* kAtk = nullptr;  Knob* kDec = nullptr;   Knob* kSus = nullptr;  Knob* kRel = nullptr;
    Knob* kStart = nullptr; Knob* kEnd = nullptr;
    Knob* kPTime = nullptr; Knob* kPCurve = nullptr; Knob* kGroup = nullptr;
    Knob* kMaxV = nullptr;  Knob* kBend = nullptr;
    Knob* kStretch = nullptr; Knob* kFormant = nullptr;
    Combo* cInterp = nullptr; Combo* cPMode = nullptr; Combo* cPShape = nullptr; Combo* cPoly = nullptr;
    Combo* cAlgo = nullptr; Combo* cDur = nullptr; Combo* cSync = nullptr;

    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerEditor)
};
