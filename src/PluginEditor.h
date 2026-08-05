#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "gui/WaveformView.h"
#include "gui/DropZone.h"

//==============================================================================
class OtoMadSamplerEditor : public juce::AudioProcessorEditor
{
public:
    explicit OtoMadSamplerEditor (OtoMadSamplerProcessor&);
    ~OtoMadSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
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

    OtoMadSamplerProcessor& processor;

    otomad::gui::WaveformView waveform;
    otomad::gui::DropZone     dropZone;
    juce::MidiKeyboardComponent keyboard;

    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox interpBox;
    juce::Label    interpLabel;
    std::unique_ptr<ComboAttach> interpAttach;

    juce::ToggleButton snapButton { "Snap Zero-Cross" };
    std::unique_ptr<ButtonAttach> snapAttach;

    // 個別参照が要るノブ（レイアウトで領域分けするため）
    Knob* kSemi = nullptr;  Knob* kCents = nullptr; Knob* kRoot = nullptr;
    Knob* kAtk  = nullptr;  Knob* kDec   = nullptr; Knob* kSus  = nullptr; Knob* kRel = nullptr;
    Knob* kGain = nullptr;  Knob* kStart = nullptr; Knob* kEnd  = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerEditor)
};
