#include "SampleLoader.h"

#include <algorithm>
#include <cmath>

namespace otomad::SampleLoader
{

using i64 = std::int64_t;

static void computePeaks (SampleBuffer& sb)
{
    sb.peaks.clear();
    const i64 n = sb.numSamples;
    if (n <= 0 || sb.numChannels <= 0)
        return;

    constexpr int maxBuckets = 4000;
    const i64 spb = std::max<i64> (1, n / maxBuckets);
    sb.peaks.reserve ((std::size_t) (n / spb + 1));

    for (i64 pos = 0; pos < n; pos += spb)
    {
        float mn =  1.0e9f;
        float mx = -1.0e9f;
        const i64 e = std::min<i64> (n, pos + spb);
        for (i64 i = pos; i < e; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < sb.numChannels; ++ch)
                m += sb.data[(std::size_t) ch][(std::size_t) i];
            m /= (float) sb.numChannels;
            mn = std::min (mn, m);
            mx = std::max (mx, m);
        }
        sb.peaks.emplace_back (mn, mx);
    }
}

// reader から SampleBuffer を構築（原音保持＋ホストSRへ変換＋peaks）。
static std::shared_ptr<SampleBuffer> buildFromReader (juce::AudioFormatReader& reader, double hostSampleRate)
{
    const int    numCh     = (int) reader.numChannels;
    const i64    nativeLen = (i64) reader.lengthInSamples;
    const double nativeSR  = reader.sampleRate;
    if (numCh <= 0 || nativeLen <= 0 || nativeSR <= 0.0)
        return nullptr;

    juce::AudioBuffer<float> tmp (numCh, (int) nativeLen);
    reader.read (&tmp, 0, (int) nativeLen, 0, true, true);

    auto sb = std::make_shared<SampleBuffer>();
    sb->numChannels        = numCh;
    sb->originalSampleRate  = nativeSR;

    sb->original.assign ((std::size_t) numCh, {});
    for (int ch = 0; ch < numCh; ++ch)
        sb->original[(std::size_t) ch].assign (tmp.getReadPointer (ch),
                                               tmp.getReadPointer (ch) + nativeLen);

    if (std::abs (nativeSR - hostSampleRate) < 1.0e-6)
    {
        sb->data       = sb->original;
        sb->numSamples = nativeLen;
        sb->sampleRate = hostSampleRate;
    }
    else
    {
        const double ratio  = nativeSR / hostSampleRate;
        const i64    outLen = (i64) std::ceil ((double) nativeLen * hostSampleRate / nativeSR);
        sb->data.assign ((std::size_t) numCh, std::vector<float> ((std::size_t) outLen, 0.0f));
        for (int ch = 0; ch < numCh; ++ch)
        {
            std::vector<float> in = sb->original[(std::size_t) ch];
            in.resize ((std::size_t) (nativeLen + 8), 0.0f);
            juce::LagrangeInterpolator interp;
            interp.reset();
            interp.process (ratio, in.data(), sb->data[(std::size_t) ch].data(), (int) outLen);
        }
        sb->numSamples = outLen;
        sb->sampleRate = hostSampleRate;
    }

    computePeaks (*sb);
    return sb;
}

std::shared_ptr<SampleBuffer> loadFile (const juce::File& file,
                                        double hostSampleRate,
                                        juce::AudioFormatManager& fm)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    auto sb = buildFromReader (*reader, hostSampleRate);
    if (sb != nullptr)
    {
        sb->name = file.getFileName().toStdString();
        sb->path = file.getFullPathName().toStdString();
    }
    return sb;
}

std::shared_ptr<SampleBuffer> loadFromMemory (const void* data, std::size_t size,
                                              const juce::String& displayName,
                                              double hostSampleRate,
                                              juce::AudioFormatManager& fm)
{
    if (data == nullptr || size == 0)
        return nullptr;

    // 内部コピーを持たせる（呼び出し元のバッファ寿命に依存しないようにする）
    auto stream = std::make_unique<juce::MemoryInputStream> (data, size, true);
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::move (stream)));
    if (reader == nullptr)
        return nullptr;

    auto sb = buildFromReader (*reader, hostSampleRate);
    if (sb != nullptr)
        sb->name = displayName.toStdString();   // path は不明（D&Dでバイト列のみ受領）
    return sb;
}

std::shared_ptr<SampleBuffer> loadFromFlacMemory (const void* data, std::size_t size,
                                                  double hostSampleRate)
{
    if (data == nullptr || size == 0)
        return nullptr;
    juce::FlacAudioFormat flac;
    auto* stream = new juce::MemoryInputStream (data, size, false);
    std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (stream, true));
    if (reader == nullptr)
        return nullptr;   // createReaderFor が失敗時に stream を解放する
    return buildFromReader (*reader, hostSampleRate);
}

bool encodeOriginalToFlac (const SampleBuffer& sb, juce::MemoryBlock& out, float& normScale)
{
    const int numCh = sb.numChannels;
    const i64 n     = (i64) (sb.original.empty() ? 0 : sb.original[0].size());
    if (numCh <= 0 || n <= 0)
        return false;

    // FLACは整数PCM。|x|>1 のfloatはクリップするので、ピークで正規化して係数を保存する（§3.9）。
    float peak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        for (float v : sb.original[(std::size_t) ch])
            peak = std::max (peak, std::abs (v));
    normScale = peak > 1.0f ? peak : 1.0f;

    juce::AudioBuffer<float> buf (numCh, (int) n);
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buf.getWritePointer (ch);
        for (i64 i = 0; i < n; ++i)
            d[i] = sb.original[(std::size_t) ch][(std::size_t) i] / normScale;
    }

    juce::FlacAudioFormat flac;
    auto* mos = new juce::MemoryOutputStream (out, false);
    std::unique_ptr<juce::AudioFormatWriter> writer (
        flac.createWriterFor (mos, sb.originalSampleRate, (unsigned int) numCh, 24, {}, 0));
    if (writer == nullptr) { delete mos; return false; }
    return writer->writeFromAudioSampleBuffer (buf, 0, (int) n);
}

} // namespace otomad::SampleLoader
