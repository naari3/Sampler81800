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

    // 画面キーボードは固定ベロシティ（クリック位置依存で小さくなるのを防ぐ）
    keyboard.setVelocity (0.9f, false);

    addAndMakeVisible (normalizeButton);
    normalizeButton.onClick = [this] { processor.normalizeSample(); };

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
    kStretch = &addKnob (P::stretchAmount, "Stretch", rotary);
    kFormant = &addKnob (P::formant,       "Formnt",  rotary);

    // REAPER Mode / Sub は動的名リストのコンボ（アタッチメント無し・手動でパラメータへ）
    rModeLbl.setText ("R.Mode", juce::dontSendNotification); rModeLbl.setJustificationType (juce::Justification::centred);
    rSubLbl.setText  ("R.Sub",  juce::dontSendNotification); rSubLbl.setJustificationType  (juce::Justification::centred);
    addAndMakeVisible (rModeLbl); addAndMakeVisible (rSubLbl);
    addAndMakeVisible (rModeBox); addAndMakeVisible (rSubBox);
    rModeBox.onChange = [this] { if (rModeBox.getSelectedId() > 0) setIntParam (P::reaperMode,    rModeBox.getSelectedId() - 1); };
    rSubBox.onChange  = [this] { if (rSubBox.getSelectedId()  > 0) setIntParam (P::reaperSubMode, rSubBox.getSelectedId()  - 1); };

    cInterp = &addCombo (P::interpQuality, "Interp", { "Linear", "Hermite" });
    cPMode  = &addCombo (P::portaMode,     "Porta",  { "Off", "Legato", "Always" });
    cPShape = &addCombo (P::portaShape,    "Shape",  { "Time", "Analog" });
    cPoly   = &addCombo (P::polyMode,      "Poly",   { "Mono", "Poly" });
    cAlgo   = &addCombo (P::algorithm,     "Algo",
                         { "Varispeed", "WSOLA", "Phase Vocoder", "Granular", "Stretch Library", "REAPER Shifter" });
    cDur    = &addCombo (P::durationMode,  "Duration", { "Natural", "Sync", "Manual" });
    cSync   = &addCombo (P::syncLength,    "Sync",   { "1/4", "1/2", "1", "2", "4" });

    addAndMakeVisible (snapButton);
    snapAttach = std::make_unique<ButtonAttach> (processor.getAPVTS(), P::snapZeroCross, snapButton);
    addAndMakeVisible (phaseLockButton);
    phaseLockAttach = std::make_unique<ButtonAttach> (processor.getAPVTS(), P::phaseLock, phaseLockButton);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible (statusLabel);

    reaperModeLabel.setJustificationType (juce::Justification::centredLeft);
    reaperModeLabel.setColour (juce::Label::textColourId, juce::Colours::aqua);
    reaperModeLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (reaperModeLabel);

    startTimerHz (8);
    setSize (860, 620);
}

OtoMadSamplerEditor::~OtoMadSamplerEditor() { stopTimer(); }

void OtoMadSamplerEditor::setIntParam (const juce::String& id, int value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (processor.getAPVTS().getParameter (id)))
        *p = value;
}

void OtoMadSamplerEditor::timerCallback()
{
    processor.serviceCache();   // ピッチキャッシュの保守（背景レンダリング要求の処理）

    auto& apvts = processor.getAPVTS();
    const int algo = (int) apvts.getRawParameterValue (P::algorithm)->load();
    const int dur  = (int) apvts.getRawParameterValue (P::durationMode)->load();

    // モード名コンボを一度だけ埋める（REAPER利用可時）
    if (processor.isReaperAvailable() && ! modePopulated)
    {
        const auto names = processor.getReaperModeNames();
        rModeBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            rModeBox.addItem (names[i].isEmpty() ? ("mode " + juce::String (i)) : names[i], i + 1);
        modePopulated = names.size() > 0;
    }

    const int rm  = (int) apvts.getRawParameterValue (P::reaperMode)->load();
    const int rsm = (int) apvts.getRawParameterValue (P::reaperSubMode)->load();

    // モードが変わったらサブモード名コンボを詰め直す
    if (processor.isReaperAvailable() && rm != subPopulatedForMode)
    {
        subPopulatedForMode = rm;
        const auto subs = processor.getReaperSubModeNames (rm);
        rSubBox.clear (juce::dontSendNotification);
        for (int i = 0; i < subs.size(); ++i)
            rSubBox.addItem (subs[i].isEmpty() ? ("sub " + juce::String (i)) : subs[i], i + 1);
    }

    // 現在のパラメータ値をコンボ選択に反映（onChangeを起こさない）
    if (rModeBox.getSelectedId() != rm + 1)
        rModeBox.setSelectedId (rm + 1, juce::dontSendNotification);
    if (rSubBox.getNumItems() > 0)
    {
        const int subId = juce::jlimit (1, rSubBox.getNumItems(), rsm + 1);
        if (rSubBox.getSelectedId() != subId)
            rSubBox.setSelectedId (subId, juce::dontSendNotification);
    }

    // Mode / Sub が変わったら安全に再設定＋レイテンシ再報告
    if (rm != lastReaperMode || rsm != lastReaperSubMode)
    {
        lastReaperMode = rm;
        lastReaperSubMode = rsm;
        processor.reconfigureReaperMode();
    }

    // 現在の REAPER モード名/レイテンシを表示（REAPER Shifter 選択時のみ）
    reaperModeLabel.setText (algo == 5 ? processor.getReaperModeText() : juce::String(),
                             juce::dontSendNotification);

    juce::String msg;
    if (processor.isEngineFallbackActive())   // 未実装/非対応(REAPER非対応ホスト等) → 代替再生 (規約3)
        msg = juce::CharPointer_UTF8 ("\xe2\x9a\xa0 \xe3\x81\x93\xe3\x81\xae\xe3\x82\xa8\xe3\x83\xb3\xe3\x82\xb8\xe3\x83\xb3\xe3\x81\xaf\xe6\x9c\xac\xe7\x92\xb0\xe5\xa2\x83\xe3\x81\xa7\xe4\xbd\xbf\xe3\x81\x88\xe3\x81\xbe\xe3\x81\x9b\xe3\x82\x93 \xe2\x80\x94 Phase Vocoder \xe3\x81\xa7\xe4\xbb\xa3\xe6\x9b\xbf\xe5\x86\x8d\xe7\x94\x9f\xe4\xb8\xad");
    else if (algo == 0 && dur != 0)   // Varispeed は長さが音程に従属 (§6.3)
        msg = juce::CharPointer_UTF8 ("Varispeed \xe3\x81\xa7\xe3\x81\xaf\xe9\x95\xb7\xe3\x81\x95\xe3\x81\x8c\xe9\x9f\xb3\xe7\xa8\x8b\xe3\x81\xab\xe5\xbe\x93\xe5\xb1\x9e\xef\xbc\x88Natural \xe6\x89\xb1\xe3\x81\x84\xef\xbc\x89");
    statusLabel.setText (msg, juce::dontSendNotification);
}

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

    auto waveArea = r.removeFromTop (124).reduced (8, 4);
    waveform.setBounds (waveArea);
    dropZone.setBounds (waveArea);

    auto trimRow = r.removeFromTop (40).reduced (8, 4);
    normalizeButton.setBounds (trimRow.removeFromRight (92).reduced (2, 4));
    kStart->label.setBounds (trimRow.removeFromLeft (44));
    kStart->slider.setBounds (trimRow.removeFromLeft (trimRow.getWidth() / 2 - 4).withTrimmedRight (4));
    kEnd->label.setBounds (trimRow.removeFromLeft (40));
    kEnd->slider.setBounds (trimRow);

    keyboard.setBounds (r.removeFromBottom (68).reduced (8, 4));
    reaperModeLabel.setBounds (r.removeFromBottom (18).reduced (10, 0));
    statusLabel.setBounds (r.removeFromBottom (20).reduced (10, 0));

    auto grid = r.reduced (8, 4);
    const int rowH = grid.getHeight() / 4;
    auto rowRect = [&] { return grid.removeFromTop (rowH); };
    auto cell = [] (juce::Rectangle<int>& row, int cols) { return row.removeFromLeft (row.getWidth() / cols).reduced (4); };

    // row1: Semi Cent Root Gain [Interp] Snap
    {
        auto row = rowRect(); int c = 6;
        place (kSemi, cell (row, c)); place (kCents, cell (row, c - 1));
        place (kRoot, cell (row, c - 2)); place (kGain, cell (row, c - 3));
        placeCombo (cInterp, cell (row, c - 4));
        snapButton.setBounds (row.reduced (4).withTrimmedTop (16));
    }
    // row2: A D S R Voices Bend
    {
        auto row = rowRect(); int c = 6;
        for (auto* k : { kAtk, kDec, kSus, kRel, kMaxV, kBend })
            place (k, row.removeFromLeft (row.getWidth() / c-- ).reduced (4));
    }
    // row3: Glide Curve Group [Porta][Shape][Poly]
    {
        auto row = rowRect(); int c = 6;
        place (kPTime, cell (row, c)); place (kPCurve, cell (row, c - 1)); place (kGroup, cell (row, c - 2));
        placeCombo (cPMode, cell (row, c - 3)); placeCombo (cPShape, cell (row, c - 4)); placeCombo (cPoly, cell (row, c - 5));
    }
    // row4: [Algo][Duration][Sync] Stretch Formant [R.Mode combo][R.Sub combo] [PhaseLock+CurveDisplay]
    {
        auto row = grid; int c = 8;
        placeCombo (cAlgo, cell (row, c)); placeCombo (cDur, cell (row, c - 1)); placeCombo (cSync, cell (row, c - 2));
        place (kStretch, cell (row, c - 3)); place (kFormant, cell (row, c - 4));
        auto placeRawCombo = [] (juce::Label& lbl, juce::ComboBox& box, juce::Rectangle<int> a)
        {
            lbl.setBounds (a.removeFromTop (15));
            box.setBounds (a.removeFromTop (24));
        };
        placeRawCombo (rModeLbl, rModeBox, cell (row, c - 5));
        placeRawCombo (rSubLbl,  rSubBox,  cell (row, c - 6));
        auto last = row.reduced (4);
        phaseLockButton.setBounds (last.removeFromTop (22));
        curveDisplay.setBounds (last);
    }
}
