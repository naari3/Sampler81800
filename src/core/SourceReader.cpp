#include "SourceReader.h"

#include <algorithm>
#include <cstdlib>

namespace otomad
{

std::int64_t SourceReader::snapToRisingZeroCross (const SampleBuffer& b, int ch,
                                                  std::int64_t pos, std::int64_t window) noexcept
{
    const std::int64_t n = b.numSamples;
    if (n < 2)
        return pos;

    const std::int64_t lo = std::max<std::int64_t> (1, pos - window);
    const std::int64_t hi = std::min<std::int64_t> (n - 1, pos + window);

    std::int64_t best     = pos;
    std::int64_t bestDist = -1;
    for (std::int64_t j = lo; j <= hi; ++j)
    {
        const float prev = b.sampleAtRaw (ch, j - 1);
        const float cur  = b.sampleAtRaw (ch, j);
        if (prev <= 0.0f && cur > 0.0f)
        {
            const std::int64_t d = (j >= pos) ? (j - pos) : (pos - j);
            if (bestDist < 0 || d < bestDist)
            {
                bestDist = d;
                best     = j;
            }
        }
    }
    return best;
}

void SourceReader::configure (const SampleBuffer* buf,
                              std::int64_t startSample,
                              std::int64_t endSample,
                              bool snapZeroCross) noexcept
{
    buffer = buf;

    if (buf == nullptr || buf->numSamples <= 0)
    {
        start = end = trimLen = 0;
        numCh = 0;
        sr    = 0.0;
        return;
    }

    numCh = buf->numChannels;
    sr    = buf->sampleRate;

    const std::int64_t n = buf->numSamples;
    std::int64_t s = std::clamp<std::int64_t> (startSample, 0, n);
    std::int64_t e = std::clamp<std::int64_t> (endSample,   0, n);
    if (e <= s)
        e = std::min<std::int64_t> (n, s + 1);

    if (snapZeroCross && numCh > 0)
    {
        const std::int64_t window  = (std::int64_t) (0.002 * sr);   // ±2ms
        const std::int64_t snapped = snapToRisingZeroCross (*buf, 0, s, window);
        if (snapped >= 0 && snapped < e)
            s = snapped;
    }

    start   = s;
    end     = e;
    trimLen = e - s;
}

} // namespace otomad
