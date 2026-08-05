#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Phase 0 の最小エディタ。名前とフェーズ表示だけ。
    Phase 1 以降で DropZone / WaveformView / CurveEditor に置き換わる (DESIGN.md §6)。
*/
class OtoMadSamplerEditor : public juce::AudioProcessorEditor
{
public:
    explicit OtoMadSamplerEditor (OtoMadSamplerProcessor&);
    ~OtoMadSamplerEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OtoMadSamplerProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtoMadSamplerEditor)
};
