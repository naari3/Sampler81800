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

// 埋め込みFLAC（原音）から復元。失敗時 nullptr。
std::shared_ptr<SampleBuffer> loadFromFlacMemory (const void* data, std::size_t size,
                                                  double hostSampleRate);

// 原音を FLAC(24bit)へエンコード。|x|>1 は normScale で正規化して保存（復元時に戻す）。
bool encodeOriginalToFlac (const SampleBuffer& sb, juce::MemoryBlock& out, float& normScale);

} // namespace otomad::SampleLoader
