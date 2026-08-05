#include "PhaseVocoderEngine.h"
#include "pitch/dsp/Fft.h"
#include "pitch/dsp/Interpolators.h"

#include <algorithm>
#include <cmath>

namespace otomad
{

static constexpr float kPi    = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;

static inline float princarg (float x) noexcept
{
    return x - kTwoPi * std::round (x / kTwoPi);
}

void PhaseVocoderEngine::prepare (const PitchEngineContext&, EngineResources& r)
{
    N      = r.fftSize;
    hop    = r.pvHop;
    nbins  = N / 2 + 1;
    hann   = r.hannFft.data();
    prepCh = 2;
    cap    = (long) N * 4;

    acc.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) N, 0.0f));
    accW.assign ((std::size_t) N, 0.0f);
    intRing.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) cap, 0.0f));
    prevPhase.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) nbins, 0.0f));
    sumPhase.assign ((std::size_t) prepCh, std::vector<float> ((std::size_t) nbins, 0.0f));

    spec.assign ((std::size_t) N, {});
    mag.assign ((std::size_t) nbins, 0.0f);
    phi.assign ((std::size_t) nbins, 0.0f);
    magShift.assign ((std::size_t) nbins, 0.0f);

    reset();
}

void PhaseVocoderEngine::reset()
{
    needInit = true; firstFrame = true;
    analysisPos = 0.0; intReadPos = 0.0; intWrite = 0;
    for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0f);
    std::fill (accW.begin(), accW.end(), 0.0f);
    for (auto& r : intRing) std::fill (r.begin(), r.end(), 0.0f);
    for (auto& p : prevPhase) std::fill (p.begin(), p.end(), 0.0f);
    for (auto& p : sumPhase)  std::fill (p.begin(), p.end(), 0.0f);
}

float PhaseVocoderEngine::intAt (int ch, long idx) const noexcept
{
    if (idx < 0 || idx >= intWrite || idx < intWrite - cap)
        return 0.0f;
    return intRing[(std::size_t) ch][(std::size_t) (idx % cap)];
}

void PhaseVocoderEngine::synthesizeFrame (SourceReader& src, double pitchRatio, double timeRatio) noexcept
{
    const int    nch    = std::min (prepCh, std::max (1, src.getNumChannels()));
    const double hopAna = (double) hop * timeRatio / std::max (1.0e-6, pitchRatio);
    const long   start  = std::lround (analysisPos);

    const bool doFormant = std::abs (formantSemi) > 0.01f;
    const float fRatio   = std::pow (2.0f, formantSemi / 12.0f);

    for (int ch = 0; ch < nch; ++ch)
    {
        // 解析: 窓かけ → FFT
        for (int k = 0; k < N; ++k)
            spec[(std::size_t) k] = std::complex<float> (src.sampleAt (ch, start + k) * hann[k], 0.0f);
        dsp::fftRadix2 (spec.data(), N, false);

        for (int k = 0; k < nbins; ++k)
        {
            mag[(std::size_t) k] = std::abs (spec[(std::size_t) k]);
            phi[(std::size_t) k] = std::arg (spec[(std::size_t) k]);
        }

        auto& sp = sumPhase[(std::size_t) ch];
        auto& pp = prevPhase[(std::size_t) ch];

        if (firstFrame)
        {
            for (int k = 0; k < nbins; ++k)
                sp[(std::size_t) k] = phi[(std::size_t) k];
        }
        else
        {
            for (int k = 0; k < nbins; ++k)
            {
                const float expected = kTwoPi * (float) hopAna * (float) k / (float) N;
                const float dphi = princarg (phi[(std::size_t) k] - pp[(std::size_t) k] - expected);
                const float omega = kTwoPi * (float) k / (float) N + dphi / (float) hopAna;   // rad/sample
                sp[(std::size_t) k] += omega * (float) hop;
            }
        }
        for (int k = 0; k < nbins; ++k)
            pp[(std::size_t) k] = phi[(std::size_t) k];

        // フォルマント（任意・粗い包絡シフト）。formant=0 のときは恒等。
        const float* useMag = mag.data();
        if (doFormant)
        {
            // 移動平均で包絡を作り、比 fRatio で周波数方向にリサンプルして掛け替える
            constexpr int half = 12;
            for (int k = 0; k < nbins; ++k)
            {
                float s = 0.0f; int c = 0;
                for (int j = -half; j <= half; ++j)
                {
                    const int kk = k + j;
                    if (kk >= 0 && kk < nbins) { s += mag[(std::size_t) kk]; ++c; }
                }
                magShift[(std::size_t) k] = c > 0 ? s / (float) c : 0.0f;   // env[k]
            }
            for (int k = 0; k < nbins; ++k)
            {
                const float srcBin = (float) k / fRatio;
                const int   b = std::clamp ((int) srcBin, 0, nbins - 1);
                const float envK  = magShift[(std::size_t) k] + 1.0e-9f;
                const float envSh = magShift[(std::size_t) b];
                phi[(std::size_t) k] = mag[(std::size_t) k] * (envSh / envK);   // phi 使い回し=出力mag
            }
            useMag = phi.data();
        }

        // 合成スペクトル
        for (int k = 0; k < nbins; ++k)
            spec[(std::size_t) k] = std::polar (useMag[(std::size_t) k], sp[(std::size_t) k]);
        for (int k = 1; k < nbins - 1; ++k)
            spec[(std::size_t) (N - k)] = std::conj (spec[(std::size_t) k]);

        dsp::fftRadix2 (spec.data(), N, true);

        auto& a = acc[(std::size_t) ch];
        for (int k = 0; k < N; ++k)
            a[(std::size_t) k] += spec[(std::size_t) k].real() * hann[k];
    }

    for (int k = 0; k < N; ++k)
        accW[(std::size_t) k] += hann[k] * hann[k];

    for (int k = 0; k < hop; ++k)
    {
        const long idx = intWrite + k;
        const float den = accW[(std::size_t) k];
        for (int ch = 0; ch < prepCh; ++ch)
        {
            const float v = (ch < nch && den > 1.0e-6f) ? acc[(std::size_t) ch][(std::size_t) k] / den : 0.0f;
            intRing[(std::size_t) ch][(std::size_t) (idx % cap)] = v;
        }
    }
    intWrite += hop;

    for (int ch = 0; ch < prepCh; ++ch)
    {
        auto& a = acc[(std::size_t) ch];
        std::copy (a.begin() + hop, a.end(), a.begin());
        std::fill (a.end() - hop, a.end(), 0.0f);
    }
    std::copy (accW.begin() + hop, accW.end(), accW.begin());
    std::fill (accW.end() - hop, accW.end(), 0.0f);

    firstFrame = false;
    analysisPos += hopAna;
}

void PhaseVocoderEngine::process (SourceReader& src, double& srcPos,
                                  float* const* out, int numChannels, int n,
                                  const float* pitchRatio, double timeRatio)
{
    if (needInit)
    {
        analysisPos = srcPos;
        needInit = false;
    }

    const int nch = std::min (numChannels, prepCh);

    for (int i = 0; i < n; ++i)
    {
        const double pr = (double) pitchRatio[i];

        while (intWrite < (long) std::floor (intReadPos) + 2)
            synthesizeFrame (src, pr, timeRatio);

        const long  i0 = (long) std::floor (intReadPos);
        const float t  = (float) (intReadPos - (double) i0);
        for (int ch = 0; ch < nch; ++ch)
            out[ch][i] = dsp::hermite4 (intAt (ch, i0 - 1), intAt (ch, i0),
                                        intAt (ch, i0 + 1), intAt (ch, i0 + 2), t);

        intReadPos += pr;
    }

    srcPos = analysisPos;
}

} // namespace otomad
