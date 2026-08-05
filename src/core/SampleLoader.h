#pragma once

#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>

#include "SampleBuffer.h"

namespace otomad::SampleLoader
{

// ファイルを読み込み、原音を保持しつつ data をホストSRへ変換した SampleBuffer を返す。
// 失敗時 nullptr。呼び出しは非RTスレッド（バックグラウンド）から。DESIGN.md §3.1。
std::shared_ptr<SampleBuffer> loadFile (const juce::File& file,
                                        double hostSampleRate,
                                        juce::AudioFormatManager& formatManager);

} // namespace otomad::SampleLoader
