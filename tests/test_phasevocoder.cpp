#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "test_engine_helpers.h"
#include "pitch/PhaseVocoderEngine.h"

using namespace otomad;
using Catch::Approx;

namespace
{
EngineResources makeRes () { EngineResources r; r.prepare (48000.0); return r; }
const double tenCents = std::pow (2.0, 10.0 / 1200.0) - 1.0;
}

// (a) +7半音で F0 が期待値 ±10cent
TEST_CASE ("PhaseVocoderEngine shifts +7 semitones within 10 cents", "[pv]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.5);
    PhaseVocoderEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);
    auto out = test::renderEngine (e, res, src, ratio, 1.0, 48000, 256);

    const double f0 = test::estimateF0 (out.data() + 8000, 32000, 48000.0, 0.02f);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (tenCents));
}

// (b) Natural(timeRatio=1) で長さ保持
TEST_CASE ("PhaseVocoderEngine preserves duration at timeRatio 1", "[pv]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    PhaseVocoderEngine e;

    auto out = test::renderEngine (e, res, src, std::pow (2.0, 7.0 / 12.0), 1.0, 48000 + 8192, 256);
    REQUIRE ((double) test::nonZeroExtent (out) == Approx (48000.0).epsilon (0.06));
}

// (c) timeRatio=0.5 で約2倍、pitchRatio=1 で F0 不変
TEST_CASE ("PhaseVocoderEngine stretches to 2x at timeRatio 0.5", "[pv]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    PhaseVocoderEngine e;

    auto out = test::renderEngine (e, res, src, 1.0, 0.5, 96000 + 8192, 256);
    REQUIRE ((double) test::nonZeroExtent (out) == Approx (96000.0).epsilon (0.06));

    const double f0 = test::estimateF0 (out.data() + 8000, 80000, 48000.0, 0.02f);
    REQUIRE (f0 == Approx (440.0).epsilon (tenCents));
}

// (e) ブロック分割不変性
TEST_CASE ("PhaseVocoderEngine output is independent of block size", "[pv]")
{
    auto res = makeRes();
    auto src = test::makeSine (330.0, 48000.0, 0.5);
    PhaseVocoderEngine e;

    const double ratio = std::pow (2.0, 5.0 / 12.0);
    auto a = test::renderEngine (e, res, src, ratio, 1.0, 8000, 256);
    auto b = test::renderEngine (e, res, src, ratio, 1.0, 8000, 1);

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));
    REQUIRE (maxDiff < 1.0e-3f);
}

// 位相ロック ON/OFF どちらでも F0 が保たれる（回帰）
TEST_CASE ("PhaseVocoderEngine phase lock keeps pitch", "[pv]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.5);
    const double ratio = std::pow (2.0, 7.0 / 12.0);

    for (bool lock : { true, false })
    {
        PhaseVocoderEngine e;
        e.setPhaseLock (lock);
        auto out = test::renderEngine (e, res, src, ratio, 1.0, 48000, 256);
        const double f0 = test::estimateF0 (out.data() + 8000, 32000, 48000.0, 0.02f);
        REQUIRE (f0 == Approx (440.0 * ratio).epsilon (tenCents));
    }
}

// 無音入力で NaN/Inf を出さない
TEST_CASE ("PhaseVocoderEngine is finite on silence", "[pv]")
{
    auto res = makeRes();
    SampleBuffer sil;
    sil.numChannels = 1; sil.numSamples = 24000; sil.sampleRate = 48000.0;
    sil.data.assign (1, std::vector<float> (24000, 0.0f));

    PhaseVocoderEngine e;
    auto out = test::renderEngine (e, res, sil, std::pow (2.0, 7.0 / 12.0), 1.0, 24000, 256);
    for (float v : out) REQUIRE (std::isfinite (v));
}

// (f) フォルマントシフトがレベルを暴走させない
TEST_CASE ("PhaseVocoderEngine formant shift does not blow up the level", "[pv][formant]")
{
    // 包絡を割り算でワープさせる実装は、素直に書くと**信号の無い帯域で必ず破綻する**。
    // env[k] がほぼ 0 の高域で除算が青天井のゲインになり、ノイズフロアを持ち上げる。
    // さらに +12半音なら 8-20kHz の参照先が 4-10kHz の実信号になるので、
    // 元々何も無いところに音を作ってしまう。
    //
    // 実測（F0=150Hz に 700/1200/2600Hz のフォルマントを載せた母音風信号）:
    //   対策前 formant=+12 で ピーク 20.2倍 / 8-20kHz が入力比 +34.5dB
    //   対策後 formant=+12 で ピーク  1.53倍 / 8-20kHz は入力比 +15dB 相当まで低下
    const double sr = 48000.0;
    const int    n  = (int) (sr * 0.5);

    // 母音っぽい信号（倍音列にフォルマントを載せる）
    SampleBuffer src;
    src.numChannels = 1;
    src.sampleRate  = sr;
    src.numSamples  = n;
    std::vector<float> mono ((std::size_t) n, 0.0f);
    for (int h = 1; h * 150.0 < 8000.0; ++h)
    {
        const double f = h * 150.0;
        double a = 0.0;
        for (double fc : { 700.0, 1200.0, 2600.0 })
            a += 1.0 / (1.0 + std::pow ((f - fc) / 220.0, 2.0));
        for (int i = 0; i < n; ++i)
            mono[(std::size_t) i] += (float) (0.12 * a * std::sin (2.0 * 3.14159265358979 * f * i / sr));
    }
    src.data.assign (1, mono);

    double inPeak = 0.0;
    for (float v : mono) inPeak = std::max (inPeak, (double) std::abs (v));
    REQUIRE (inPeak > 0.1);

    auto res = makeRes();
    for (float semi : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        INFO ("formant " << semi);
        PhaseVocoderEngine e;
        e.setFormantShift (semi);
        auto out = test::renderEngine (e, res, src, 1.0, 1.0, n, 512);

        double peak = 0.0;
        for (float v : out) peak = std::max (peak, (double) std::abs (v));

        REQUIRE (peak > 0.05 * inPeak);   // 無音になっていない（規約15）
        REQUIRE (peak < 2.0  * inPeak);   // 暴走していない（対策前は 20倍を超えていた）
    }
}
