#pragma once

#include <cmath>
#include <vector>

#include "core/SampleBuffer.h"
#include "core/SourceReader.h"
#include "pitch/IPitchEngine.h"
#include "pitch/EngineResources.h"

namespace otomad::test
{

// エンジンを ratio/timeRatio 一定でレンダリングし、1ch 出力を返す（ブロックサイズ指定可）。
inline std::vector<float> renderEngine (IPitchEngine& e, EngineResources& res,
                                        const SampleBuffer& src,
                                        double pitchRatio, double timeRatio,
                                        int outLen, int blockSize)
{
    SourceReader reader;
    reader.configure (&src, 0, src.numSamples, false);

    PitchEngineContext ctx { src.sampleRate, blockSize, 1 };
    e.prepare (ctx, res);
    e.reset();

    std::vector<float> out ((std::size_t) outLen, 0.0f);
    std::vector<float> ratioBuf ((std::size_t) blockSize, (float) pitchRatio);

    double srcPos = 0.0;
    int pos = 0;
    while (pos < outLen)
    {
        const int nn = std::min (blockSize, outLen - pos);
        float* ptr = out.data() + pos;
        float* ptrs[1] = { ptr };
        e.process (reader, srcPos, ptrs, 1, nn, ratioBuf.data(), timeRatio);
        pos += nn;
    }
    return out;
}

// 非ゼロ区間長（|x|>thr の最初〜最後）をサンプル数で返す。
inline int nonZeroExtent (const std::vector<float>& x, float thr = 1.0e-4f)
{
    int first = -1, last = -1;
    for (int i = 0; i < (int) x.size(); ++i)
        if (std::abs (x[(std::size_t) i]) > thr)
        {
            if (first < 0) first = i;
            last = i;
        }
    return (first < 0) ? 0 : (last - first + 1);
}

} // namespace otomad::test
