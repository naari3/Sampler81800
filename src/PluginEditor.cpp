#include "PluginEditor.h"

namespace P = otomad::params;

//==============================================================================
OtoMadSamplerEditor::OtoMadSamplerEditor (OtoMadSamplerProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      waveform (p), dropZone (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      curveDisplay (p.getAPVTS())
{
    addAndMakeVisible (waveform);
    addAndMakeVisible (dropZone);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (curveDisplay);

    const auto rotary = juce::Slider::RotaryHorizontalVerticalDrag;
    const auto linear = juce::Slider::LinearHorizontal;

    kSemi  = &addKnob (P::pitchSemi,   "Semi",  rotary);
    kCents = &addKnob (P::pitchCents,  "Cent",  rotary);
    kRoot  = &addKnob (P::rootKey,     "Root",  rotary);
    kGain  = &addKnob (P::gain,        "Gain",  rotary);
    kAtk   = &addKnob (P::attack,      "A",     rotary);
    kDec   = &addKnob (P::decay,       "D",     rotary);
    kSus   = &addKnob (P::sustain,     "S",     rotary);
    kRel   = &addKnob (P::release,     "R",     rotary);
    kStart = &addKnob (P::sampleStart, "Start", linear);
    kEnd   = &addKnob (P::sampleEnd,   "End",   linear);
    kPTime = &addKnob (P::portaTime,   "Glide", rotary);
    kPCurve= &addKnob (P::portaCurve,  "Curve", rotary);
    kGroup = &addKnob (P::glideGroupMs,"Group", rotary);
    kMaxV  = &addKnob (P::maxVoices,   "Voices",rotary);
    kBend  = &addKnob (P::bendRange,   "Bend",  rotary);

    cInterp = &addCombo (P::interpQuality, "Interp", { "Linear", "Hermite" });
    cPMode  = &addCombo (P::portaMode,     "Porta",  { "Off", "Legato", "Always" });
    cPShape = &addCombo (P::portaShape,    "Shape",  { "Time", "Analog" });
    cPoly   = &addCombo (P::polyMode,      "Poly",   { "Mono", "Poly" });

    addAndMakeVisible (snapButton);
    snapAttach = std::make_unique<ButtonAttach> (processor.getAPVTS(), P::snapZeroCross, snapButton);

    setSize (760, 560);
}

OtoMadSamplerEditor::~OtoMadSamplerEditor() = default;

OtoMadSamplerEditor::Knob& OtoMadSamplerEditor::addKnob (const juce::String& paramID,
                                                         const juce::String& text,
                                                         juce::Slider::SliderStyle style)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (style);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attach = std::make_unique<SliderAttach> (processor.getAPVTS(), paramID, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

OtoMadSamplerEditor::Combo& OtoMadSamplerEditor::addCombo (const juce::String& paramID,
                                                           const juce::String& text,
                                                           const juce::StringArray& items)
{
    auto c = std::make_unique<Combo>();
    for (int i = 0; i < items.size(); ++i)
        c->box.addItem (items[i], i + 1);
    c->label.setText (text, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    c->attach = std::make_unique<ComboAttach> (processor.getAPVTS(), paramID, c->box);
    combos.push_back (std::move (c));
    return *combos.back();
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
    auto place = [] (Knob* k, juce::Rectangle<int> a)
    {
        k->label.setBounds (a.removeFromTop (15));
        k->slider.setBounds (a);
    };
    auto placeCombo = [] (Combo* c, juce::Rectangle<int> a)
    {
        c->label.setBounds (a.removeFromTop (15));
        c->box.setBounds (a.removeFromTop (24));
    };

    auto r = getLocalBounds();
    r.removeFromTop (28);

    auto waveArea = r.removeFromTop (130).reduced (8, 4);
    waveform.setBounds (waveArea);
    dropZone.setBounds (waveArea);

    auto trimRow = r.removeFromTop (44).reduced (8, 4);
    kStart->label.setBounds (trimRow.removeFromLeft (44));
    kStart->slider.setBounds (trimRow.removeFromLeft (trimRow.getWidth() / 2 - 4).withTrimmedRight (4));
    kEnd->label.setBounds (trimRow.removeFromLeft (40));
    kEnd->slider.setBounds (trimRow);

    keyboard.setBounds (r.removeFromBottom (70).reduced (8, 4));

    auto grid = r.reduced (8, 4);
    const int rowH = grid.getHeight() / 3;

    // row1: Semi Cent Root Gain + Interp
    {
        auto row = grid.removeFromTop (rowH);
        const int w = row.getWidth() / 5;
        for (auto* k : { kSemi, kCents, kRoot, kGain })
            place (k, row.removeFromLeft (w).reduced (4));
        placeCombo (cInterp, row.reduced (4));
    }
    // row2: A D S R + Snap
    {
        auto row = grid.removeFromTop (rowH);
        const int w = row.getWidth() / 5;
        for (auto* k : { kAtk, kDec, kSus, kRel })
            place (k, row.removeFromLeft (w).reduced (4));
        snapButton.setBounds (row.reduced (6).withTrimmedTop (16));
    }
    // row3: Glide Curve Group Voices Bend + Porta/Shape/Poly + CurveDisplay
    {
        auto row = grid;
        const int w = row.getWidth() / 8;
        for (auto* k : { kPTime, kPCurve, kGroup, kMaxV, kBend })
            place (k, row.removeFromLeft (w).reduced (4));
        placeCombo (cPMode,  row.removeFromLeft (w).reduced (4));
        placeCombo (cPShape, row.removeFromLeft (w).reduced (4));
        auto last = row;
        placeCombo (cPoly, last.removeFromTop (44).reduced (4));
        curveDisplay.setBounds (last.reduced (4));
    }
}
