#include "PluginEditor.h"
#include "core/Params.h"

namespace P = otomad::params;

//==============================================================================
OtoMadSamplerEditor::OtoMadSamplerEditor (OtoMadSamplerProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      waveform (p), dropZone (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (waveform);
    addAndMakeVisible (dropZone);   // waveform の上に重ねる（透明オーバーレイ）
    addAndMakeVisible (keyboard);

    auto rotary = juce::Slider::RotaryHorizontalVerticalDrag;
    auto linear = juce::Slider::LinearHorizontal;

    kSemi  = &addKnob (P::pitchSemi,   "Semi",    rotary);
    kCents = &addKnob (P::pitchCents,  "Cent",    rotary);
    kRoot  = &addKnob (P::rootKey,     "Root",    rotary);
    kAtk   = &addKnob (P::attack,      "A",       rotary);
    kDec   = &addKnob (P::decay,       "D",       rotary);
    kSus   = &addKnob (P::sustain,     "S",       rotary);
    kRel   = &addKnob (P::release,     "R",       rotary);
    kGain  = &addKnob (P::gain,        "Gain",    rotary);
    kStart = &addKnob (P::sampleStart, "Start",   linear);
    kEnd   = &addKnob (P::sampleEnd,   "End",     linear);

    interpLabel.setText ("Interp", juce::dontSendNotification);
    interpLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (interpLabel);
    interpBox.addItem ("Linear", 1);
    interpBox.addItem ("Hermite", 2);
    addAndMakeVisible (interpBox);
    interpAttach = std::make_unique<ComboAttach> (processor.getAPVTS(), P::interpQuality, interpBox);

    addAndMakeVisible (snapButton);
    snapAttach = std::make_unique<ButtonAttach> (processor.getAPVTS(), P::snapZeroCross, snapButton);

    setSize (660, 470);
}

OtoMadSamplerEditor::~OtoMadSamplerEditor() = default;

OtoMadSamplerEditor::Knob& OtoMadSamplerEditor::addKnob (const juce::String& paramID,
                                                         const juce::String& text,
                                                         juce::Slider::SliderStyle style)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (style);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attach = std::make_unique<SliderAttach> (processor.getAPVTS(), paramID, k->slider);

    knobs.push_back (std::move (k));
    return *knobs.back();
}

//==============================================================================
void OtoMadSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("OtoMadSampler", getLocalBounds().removeFromTop (26).withTrimmedLeft (10),
                juce::Justification::centredLeft, false);
}

//==============================================================================
void OtoMadSamplerEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (28);   // タイトル

    // 波形＋ドロップ（同一領域に重ねる）
    auto waveArea = r.removeFromTop (140).reduced (8, 4);
    waveform.setBounds (waveArea);
    dropZone.setBounds (waveArea);

    // トリム（linear）を波形直下に横並び
    auto trimRow = r.removeFromTop (48).reduced (8, 4);
    kStart->label.setBounds (trimRow.removeFromLeft (44));
    kStart->slider.setBounds (trimRow.removeFromLeft (trimRow.getWidth() / 2 - 4).withTrimmedRight (4));
    kEnd->label.setBounds (trimRow.removeFromLeft (40));
    kEnd->slider.setBounds (trimRow);

    // キーボードを最下段に
    keyboard.setBounds (r.removeFromBottom (72).reduced (8, 4));

    // 残りにノブを並べる
    auto knobArea = r.reduced (8, 4);
    auto placeKnob = [] (Knob* k, juce::Rectangle<int> area)
    {
        k->label.setBounds (area.removeFromTop (16));
        k->slider.setBounds (area);
    };

    Knob* row1[] = { kSemi, kCents, kRoot, kGain };
    Knob* row2[] = { kAtk, kDec, kSus, kRel };

    auto top = knobArea.removeFromTop (knobArea.getHeight() / 2);
    {
        const int w = top.getWidth() / 5;
        for (auto* k : row1)
            placeKnob (k, top.removeFromLeft (w).reduced (4));
        // interp コンボを row1 末尾に
        auto ic = top.reduced (4);
        interpLabel.setBounds (ic.removeFromTop (16));
        interpBox.setBounds (ic.removeFromTop (24));
    }
    {
        auto bot = knobArea;
        const int w = bot.getWidth() / 5;
        for (auto* k : row2)
            placeKnob (k, bot.removeFromLeft (w).reduced (4));
        snapButton.setBounds (bot.reduced (4).withTrimmedTop (18));
    }
}
