#include "VarispeedEngine.h"
#include "pitch/dsp/Interpolators.h"

#include <cmath>

namespace otomad
{

void VarispeedEngine::process (SourceReader& src, double& srcPos,
                               float* const* out, int numChannels, int n,
                               const float* pitchRatio, double /*timeRatio*/)
{
    const int srcCh = src.getNumChannels();

    for (int i = 0; i < n; ++i)
    {
        const std::int64_t i0 = (std::int64_t) std::floor (srcPos);
        const float        t  = (float) (srcPos - (double) i0);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const int sch = (srcCh > 0) ? (ch < srcCh ? ch : srcCh - 1) : 0;

            float s;
            if (quality == Quality::Linear)
                s = dsp::linear (src.sampleAt (sch, i0), src.sampleAt (sch, i0 + 1), t);
            else
                s = dsp::hermite4 (src.sampleAt (sch, i0 - 1), src.sampleAt (sch, i0),
                                   src.sampleAt (sch, i0 + 1), src.sampleAt (sch, i0 + 2), t);
            out[ch][i] = s;
        }

        srcPos += (double) pitchRatio[i];   // 長さはピッチに従属
    }
}

} // namespace otomad
