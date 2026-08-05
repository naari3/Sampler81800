#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace otomad::params
{

// パラメータID（DESIGN.md §3.7）。Phase 1 で使う分のみ定義。
// 規約10/12: 個数・レンジ・選択肢はホストによって変えない。以降のフェーズで追加はするが縮小はしない。
inline constexpr const char* pitchSemi     = "pitchSemi";
inline constexpr const char* pitchCents    = "pitchCents";
inline constexpr const char* rootKey       = "rootKey";
inline constexpr const char* interpQuality = "interpQuality";
inline constexpr const char* attack        = "attack";
inline constexpr const char* decay         = "decay";
inline constexpr const char* sustain       = "sustain";
inline constexpr const char* release       = "release";
inline constexpr const char* sampleStart   = "sampleStart";
inline constexpr const char* sampleEnd     = "sampleEnd";
inline constexpr const char* snapZeroCross = "snapZeroCross";
inline constexpr const char* gain          = "gain";

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto msRange = [] { NormalisableRange<float> r (0.0f, 5000.0f, 1.0f, 0.4f); return r; };

    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { pitchSemi, 1 },  "Pitch (semi)", -48, 48, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pitchCents, 1 }, "Pitch (cent)",
                                                       NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { rootKey, 1 },    "Root Key", 0, 127, 60));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { interpQuality, 1 }, "Interp",
                                                        StringArray { "Linear", "Hermite" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { attack, 1 },  "Attack",  msRange(), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { decay, 1 },   "Decay",   msRange(), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { sustain, 1 }, "Sustain",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { release, 1 }, "Release", msRange(), 50.0f));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { sampleStart, 1 }, "Sample Start",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { sampleEnd, 1 },   "Sample End",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 1.0f));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { snapZeroCross, 1 }, "Snap Zero-Cross", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gain, 1 }, "Gain",
                                                       NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));
    return layout;
}

} // namespace otomad::params
