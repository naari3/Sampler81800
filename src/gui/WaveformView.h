#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class OtoMadSamplerProcessor;   // グローバル名前空間の実クラス

namespace otomad::gui
{

//==============================================================================
/**
    波形表示。SampleBuffer::peaks のみ参照し、オーディオデータに直接触らない（規約3）。
    トリム範囲は APVTS の sampleStart/sampleEnd から描く。
*/
class WaveformView : public juce::Component,
                     private juce::Timer
{
public:
    explicit WaveformView (OtoMadSamplerProcessor&);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    OtoMadSamplerProcessor& processor;
    int lastSampleVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

} // namespace otomad::gui
