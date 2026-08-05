#include "Fft.h"

#include <cmath>

namespace otomad::dsp
{

void fftRadix2 (std::complex<float>* a, int n, bool inverse) noexcept
{
    if (n < 2)
        return;

    // ビット反転並べ替え
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }

    const float sign = inverse ? 1.0f : -1.0f;
    for (int len = 2; len <= n; len <<= 1)
    {
        const float ang = sign * 2.0f * 3.14159265358979323846f / (float) len;
        const std::complex<float> wlen (std::cos (ang), std::sin (ang));
        for (int i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k)
            {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse)
    {
        const float inv = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
            a[i] *= inv;
    }
}

} // namespace otomad::dsp
