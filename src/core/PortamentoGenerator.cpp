#include "PortamentoGenerator.h"

#include <algorithm>
#include <cmath>

namespace otomad
{

bool PortamentoGenerator::differs (float a, float b) const noexcept
{
    return std::abs (a - b) > 1.0e-6f;
}

void PortamentoGenerator::recalcAnalog () noexcept
{
    // portaTime を「99%到達時間」とみなし時定数 τ を逆算（6.9 ≈ ln(1000)）。§3.3
    const double tau = std::max ((double) totalMs * 0.001 / 6.9, 1.0e-6);
    analogCoeff = (float) std::exp (-1.0 / (tau * sr));
}

void PortamentoGenerator::setSampleRate (double s) noexcept
{
    sr = s > 0.0 ? s : 44100.0;
    totalSamples = (double) totalMs * 0.001 * sr;
    recalcAnalog();
}

void PortamentoGenerator::setTime (float ms) noexcept
{
    totalMs = std::max (0.0f, ms);
    totalSamples = (double) totalMs * 0.001 * sr;
    recalcAnalog();
}

void PortamentoGenerator::setCurve (float c) noexcept { curve = std::clamp (c, -1.0f, 1.0f); }
void PortamentoGenerator::setShape (Shape s) noexcept { shape = s; }

void PortamentoGenerator::startAt (float semi) noexcept
{
    startS = targetS = cur = semi;
    elapsed = 0.0;
    gliding = false;
}

void PortamentoGenerator::startGlide (float origin, float target) noexcept
{
    startS  = origin;
    cur     = origin;
    targetS = target;
    elapsed = 0.0;
    gliding = differs (origin, target) && (shape == Shape::Analog || totalSamples > 0.0);
    if (! gliding)
        cur = targetS;
}

void PortamentoGenerator::setOrigin (float origin) noexcept
{
    startS  = origin;
    cur     = origin;
    elapsed = 0.0;
    gliding = differs (origin, targetS) && (shape == Shape::Analog || totalSamples > 0.0);
    if (! gliding)
        cur = targetS;
}

void PortamentoGenerator::setTarget (float target) noexcept
{
    startS  = cur;
    targetS = target;
    elapsed = 0.0;
    gliding = differs (startS, targetS) && (shape == Shape::Analog || totalSamples > 0.0);
    if (! gliding)
        cur = targetS;
}

void PortamentoGenerator::snap () noexcept
{
    cur = targetS;
    gliding = false;
}

float PortamentoGenerator::progress () const noexcept
{
    if (! gliding)
        return 1.0f;
    if (shape == Shape::Analog)
    {
        const float span = std::abs (targetS - startS);
        if (span <= 1.0e-6f)
            return 1.0f;
        return std::clamp (1.0f - std::abs (targetS - cur) / span, 0.0f, 1.0f);
    }
    return totalSamples > 0.0 ? (float) std::clamp (elapsed / totalSamples, 0.0, 1.0) : 1.0f;
}

float PortamentoGenerator::nextSemitone () noexcept
{
    if (! gliding)
        return cur;

    if (shape == Shape::Analog)
    {
        cur += (targetS - cur) * (1.0f - analogCoeff);
        if (std::abs (targetS - cur) < 0.001f)
        {
            cur = targetS;
            gliding = false;
        }
        return cur;
    }

    // Time モード: s(p) = p^k, k = 4^(-curve)
    elapsed += 1.0;
    const double p = totalSamples > 0.0 ? std::clamp (elapsed / totalSamples, 0.0, 1.0) : 1.0;
    const float  k = std::pow (4.0f, -curve);
    const float  s = std::pow ((float) p, k);
    cur = startS + (targetS - startS) * s;
    if (p >= 1.0)
    {
        cur = targetS;
        gliding = false;
    }
    return cur;
}

void PortamentoGenerator::process (float* dst, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        dst[i] = nextSemitone();
}

} // namespace otomad
