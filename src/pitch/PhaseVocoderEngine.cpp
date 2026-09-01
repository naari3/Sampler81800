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

void PhaseVocoderEngine::prepare (const PitchEngineContext& ctx, EngineResources& r)
{
    sampleRate = ctx.sampleRate > 0.0 ? ctx.sampleRate : 48000.0;
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
    gain.assign ((std::size_t) nbins, 1.0f);
    outPhase.assign ((std::size_t) nbins, 0.0f);
    peaks.clear();
    peaks.reserve ((std::size_t) nbins);

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

        // フォルマント（包絡だけを周波数軸方向にずらす）。formant=0 のときは恒等。
        // mag をインプレース更新する。
        //
        // 包絡 env[k] を作って mag[k] *= env[k/fRatio] / env[k] とするのが基本形だが、
        // 素直に書くと**高域で必ず破綻する**。信号の無い帯域では env[k] がほぼ 0 なので
        // 除算が青天井のゲインになり、ノイズフロアを持ち上げてしまう。
        // 実測（F0=150Hz に 700/1200/2600Hz のフォルマントを載せた母音風信号, formant=+12）:
        //   対策前 ピーク 20.2 倍 / 8-20kHz が +40.2dB
        // 対策は3つ。
        //   (1) 包絡の下限をフレーム内の最大値に対する相対値で置く（絶対値の 1e-9 では効かない）
        //   (2) ゲインそのものを ±12dB に制限する。フォルマント移動は包絡の傾きの話なので、
        //       これを超える倍率が要る時点で包絡の推定が破綻している
        //   (3) 参照位置を線形補間する。切り捨てだと包絡が階段状になってざらつく
        if (doFormant)
        {
            // 平滑幅は「bin 数」ではなく「Hz」で決める。bin 数固定だと N やサンプルレートで
            // 実際の幅が変わってしまう。倍音のさざなみ(F0間隔)は均すが、フォルマント同士
            // (1kHz 程度は離れている)は潰さない幅として 300Hz を採る。
            const float binHz = (float) sampleRate / (float) N;
            const int   half  = std::clamp ((int) std::lround (150.0f / binHz), 2, nbins / 4);

            float envMax = 0.0f;
            for (int k = 0; k < nbins; ++k)
            {
                float s = 0.0f; int c = 0;
                for (int j = -half; j <= half; ++j)
                {
                    const int kk = k + j;
                    if (kk >= 0 && kk < nbins) { s += mag[(std::size_t) kk]; ++c; }
                }
                magShift[(std::size_t) k] = c > 0 ? s / (float) c : 0.0f;   // env[k]（元magから）
                envMax = std::max (envMax, magShift[(std::size_t) k]);
            }

            // (1) 相対下限。これ未満の bin は「元々そこに信号が無い」とみなす。
            //     絶対値 1e-9 では高域でまったく効かなかった。
            const float envFloor = envMax * 1.0e-2f + 1.0e-20f;
            constexpr float gMin = 0.25f, gMax = 4.0f;   // (2) ±12dB

            for (int k = 0; k < nbins; ++k)
            {
                const float envK = magShift[(std::size_t) k];

                // (4) **信号の無い帯域は触らない。** ここを等倍にしないと、
                //     たとえば +12半音では 8-20kHz の参照先が 4-10kHz の実信号になるため、
                //     元々何も無い高域にゲインが掛かってノイズフロアを持ち上げる。
                //     実測ではこれが支配的で、8-20kHz が入力比 +34dB になっていた。
                if (envK < envFloor)
                {
                    gain[(std::size_t) k] = 1.0f;
                    continue;
                }

                // (3) 線形補間で env[k / fRatio] を引く（切り捨てだと包絡が階段状になる）
                const float pos = (float) k / fRatio;
                const int   b0  = std::clamp ((int) pos, 0, nbins - 1);
                const int   b1  = std::min (b0 + 1, nbins - 1);
                const float t   = std::clamp (pos - (float) b0, 0.0f, 1.0f);
                const float envSrc = (1.0f - t) * magShift[(std::size_t) b0]
                                   +         t  * magShift[(std::size_t) b1];

                gain[(std::size_t) k] = std::clamp (envSrc / envK, gMin, gMax);
            }

            // (5) ゲイン曲線そのものを周波数方向に均す。
            //     bin ごとに角のあるゲインを掛けると、その逆変換が窓長を超えて
            //     時間方向に巻き込み（円状畳み込みの回り込み）、ざらついた音になる。
            //     クランプや下限処理で必ず角ができるので、掛ける前に鈍らせる。
            {
                const int gh = std::max (1, half / 2);
                for (int k = 0; k < nbins; ++k)
                {
                    float s = 0.0f; int c = 0;
                    for (int j = -gh; j <= gh; ++j)
                    {
                        const int kk = k + j;
                        if (kk >= 0 && kk < nbins) { s += gain[(std::size_t) kk]; ++c; }
                    }
                    magShift[(std::size_t) k] = c > 0 ? s / (float) c : 1.0f;   // 平滑後を magShift に置く
                }
                for (int k = 0; k < nbins; ++k)
                    mag[(std::size_t) k] *= magShift[(std::size_t) k];
            }
        }

        // Identity phase locking（§4.4）: ピーク位相を通常伝播し、周辺binをピークに固定する。
        const float* usePhase = sp.data();
        if (phaseLock && ! firstFrame)
        {
            peaks.clear();
            for (int k = 2; k < nbins - 2; ++k)
                if (mag[(std::size_t) k] > mag[(std::size_t) (k - 1)] && mag[(std::size_t) k] > mag[(std::size_t) (k + 1)]
                    && mag[(std::size_t) k] >= mag[(std::size_t) (k - 2)] && mag[(std::size_t) k] >= mag[(std::size_t) (k + 2)])
                    peaks.push_back (k);

            if (! peaks.empty())
            {
                int pi = 0;
                for (int k = 0; k < nbins; ++k)
                {
                    while (pi + 1 < (int) peaks.size()
                           && std::abs (peaks[(std::size_t) (pi + 1)] - k) <= std::abs (peaks[(std::size_t) pi] - k))
                        ++pi;
                    const int pk = peaks[(std::size_t) pi];
                    outPhase[(std::size_t) k] = sp[(std::size_t) pk]
                                              + (phi[(std::size_t) k] - phi[(std::size_t) pk]);
                }
                for (int k = 0; k < nbins; ++k)
                    sp[(std::size_t) k] = outPhase[(std::size_t) k];   // 次フレームへ継承
                usePhase = outPhase.data();
            }
        }

        // 合成スペクトル
        for (int k = 0; k < nbins; ++k)
            spec[(std::size_t) k] = std::polar (mag[(std::size_t) k], usePhase[(std::size_t) k]);
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
