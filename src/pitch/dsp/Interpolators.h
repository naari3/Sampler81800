#pragma once

namespace otomad::dsp
{

// 2点線形補間。
inline float linear (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

// 4点3次エルミート補間（Laurent de Soras の定式化）。
// xm1,x0,x1,x2 は連続する4サンプル、t∈[0,1) は x0 と x1 の間の位置。
inline float hermite4 (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((((a * t) - b) * t + c) * t + x0);
}

} // namespace otomad::dsp
