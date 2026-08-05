#pragma once

#include <complex>
#include <vector>

namespace otomad::dsp
{

// JUCE非依存の基数2 FFT（in-place, size は2の冪）。
// Phase 3 では正しさ優先の素朴実装。SIMD/最適化は Phase 6（設計 §2.3 の FftWrapper 相当）。
void fftRadix2 (std::complex<float>* a, int n, bool inverse) noexcept;

} // namespace otomad::dsp
