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
// Phase 2
inline constexpr const char* portaMode     = "portaMode";
inline constexpr const char* portaShape    = "portaShape";
inline constexpr const char* portaTime     = "portaTime";
inline constexpr const char* portaCurve    = "portaCurve";
inline constexpr const char* glideGroupMs  = "glideGroupMs";
inline constexpr const char* polyMode      = "polyMode";
inline constexpr const char* maxVoices     = "maxVoices";
inline constexpr const char* bendRange     = "bendRange";
// Phase 3
inline constexpr const char* algorithm     = "algorithm";
inline constexpr const char* durationMode  = "durationMode";
inline constexpr const char* syncLength    = "syncLength";
inline constexpr const char* stretchAmount = "stretchAmount";
inline constexpr const char* formant       = "formant";
inline constexpr const char* reaperSubMode = "reaperSubMode";

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

    // Phase 2: ポルタメント / ポリ / ベンド
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { portaMode, 1 }, "Porta Mode",
                                                        StringArray { "Off", "Legato", "Always" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { portaShape, 1 }, "Porta Shape",
                                                        StringArray { "Time", "Analog" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { portaTime, 1 }, "Porta Time",
                                                       msRange(), 80.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { portaCurve, 1 }, "Porta Curve",
                                                       NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { glideGroupMs, 1 }, "Glide Group",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { polyMode, 1 }, "Poly Mode",
                                                        StringArray { "Mono", "Poly" }, 0));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { maxVoices, 1 }, "Max Voices", 1, 16, 8));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { bendRange, 1 }, "Bend Range", 0, 48, 2));

    // Phase 3: アルゴリズム / 長さ制御 / フォルマント（規約10/12: 選択肢は6個で固定, 縮小しない）
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { algorithm, 1 }, "Algorithm",
                    StringArray { "Varispeed", "WSOLA", "Phase Vocoder", "Granular", "Stretch Library", "REAPER Shifter" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { durationMode, 1 }, "Duration",
                    StringArray { "Natural", "Sync", "Manual" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { syncLength, 1 }, "Sync Length",
                    StringArray { "1/4", "1/2", "1", "2", "4" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { stretchAmount, 1 }, "Stretch",
                    NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { formant, 1 }, "Formant",
                    NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { reaperSubMode, 1 }, "REAPER SubMode", 0, 31, 0));

    return layout;
}

} // namespace otomad::params
