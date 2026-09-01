#include "EngineResources.h"

#include <cmath>

namespace otomad
{

static void makeHann (std::vector<float>& w, int n)
{
    w.assign ((std::size_t) n, 0.0f);
    if (n <= 1)
    {
        if (n == 1) w[0] = 1.0f;
        return;
    }
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (int i = 0; i < n; ++i)
        w[(std::size_t) i] = 0.5f * (1.0f - (float) std::cos (twoPi * (double) i / (double) (n - 1)));
}

void EngineResources::prepare (double /*sampleRate*/)
{
    makeHann (hannWsola, wsolaFrame);
    makeHann (hannFft,   fftSize);
    makeHann (hannGran,  granFrame);
}

} // namespace otomad
