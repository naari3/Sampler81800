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

std::shared_ptr<SampleBuffer> loadFile (const juce::File& file,
                                        double hostSampleRate,
                                        juce::AudioFormatManager& fm)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    const int    numCh     = (int) reader->numChannels;
    const i64    nativeLen = (i64) reader->lengthInSamples;
    const double nativeSR  = reader->sampleRate;
    if (numCh <= 0 || nativeLen <= 0 || nativeSR <= 0.0)
        return nullptr;

    juce::AudioBuffer<float> tmp (numCh, (int) nativeLen);
    reader->read (&tmp, 0, (int) nativeLen, 0, true, true);

    auto sb = std::make_shared<SampleBuffer>();
    sb->name               = file.getFileName().toStdString();
    sb->numChannels        = numCh;
    sb->originalSampleRate  = nativeSR;

    // 原音を保持（保存・SR再変換の元）
    sb->original.assign ((std::size_t) numCh, {});
    for (int ch = 0; ch < numCh; ++ch)
        sb->original[(std::size_t) ch].assign (tmp.getReadPointer (ch),
                                               tmp.getReadPointer (ch) + nativeLen);

    // 再生用 data をホストSRへ
    if (std::abs (nativeSR - hostSampleRate) < 1.0e-6)
    {
        sb->data       = sb->original;
        sb->numSamples = nativeLen;
        sb->sampleRate = hostSampleRate;
    }
    else
    {
        const double ratio  = nativeSR / hostSampleRate;                       // 入力/出力
        const i64    outLen = (i64) std::ceil ((double) nativeLen * hostSampleRate / nativeSR);
        sb->data.assign ((std::size_t) numCh, std::vector<float> ((std::size_t) outLen, 0.0f));

        for (int ch = 0; ch < numCh; ++ch)
        {
            // LagrangeInterpolator は出力に対し ratio 倍の入力を読むので末尾を数サンプル余分に確保
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

} // namespace otomad::SampleLoader
