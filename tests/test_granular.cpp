#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "test_engine_helpers.h"
#include "pitch/GranularEngine.h"

using namespace otomad;
using Catch::Approx;

namespace
{
EngineResources makeRes () { EngineResources r; r.prepare (48000.0); return r; }
// 粒どうしの位相を揃えないぶん WSOLA より精度は落ちる。実測の最悪は 15cent なので 25cent を許容。
const double twentyFiveCents = std::pow (2.0, 25.0 / 1200.0) - 1.0;
}

// (a) ピッチ精度: +7半音
TEST_CASE ("GranularEngine shifts +7 semitones within 25 cents", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.5);
    GranularEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);
    auto out = test::renderEngine (e, res, src, ratio, 1.0, 48000, 256);

    const double f0 = test::estimateF0Near (out.data() + 8000, 32000, 48000.0, 440.0 * ratio);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (twentyFiveCents));
}

// (b) 出力長: Natural(timeRatio=1) で長さ保持
TEST_CASE ("GranularEngine preserves duration at timeRatio 1", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    GranularEngine e;

    auto out = test::renderEngine (e, res, src, std::pow (2.0, 7.0 / 12.0), 1.0, 48000 + 8192, 256);
    REQUIRE ((double) test::nonZeroExtent (out) == Approx (48000.0).epsilon (0.06));
}

// (c) 出力長: timeRatio=0.5 で約2倍、pitchRatio=1 なら F0 不変
TEST_CASE ("GranularEngine stretches to 2x at timeRatio 0.5", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    GranularEngine e;

    auto out = test::renderEngine (e, res, src, 1.0, 0.5, 96000 + 8192, 256);
    REQUIRE ((double) test::nonZeroExtent (out) == Approx (96000.0).epsilon (0.06));

    const double f0 = test::estimateF0Near (out.data() + 8000, 80000, 48000.0, 440.0);
    REQUIRE (f0 == Approx (440.0).epsilon (twentyFiveCents));
}

// (d) ピッチと長さが干渉しない（規約5: 癒着させない）
TEST_CASE ("GranularEngine pitch and duration are independent", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    GranularEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);
    auto out = test::renderEngine (e, res, src, ratio, 0.5, 96000 + 8192, 256);

    REQUIRE ((double) test::nonZeroExtent (out) == Approx (96000.0).epsilon (0.06));
    const double f0 = test::estimateF0Near (out.data() + 8000, 80000, 48000.0, 440.0 * ratio);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (twentyFiveCents));
}

// (e) ブロック分割不変性: blockSize 256 と 1 で一致（規約7）
TEST_CASE ("GranularEngine output is independent of block size", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (330.0, 48000.0, 0.5);
    GranularEngine e;

    const double ratio = std::pow (2.0, 5.0 / 12.0);
    auto a = test::renderEngine (e, res, src, ratio, 1.0, 8000, 256);
    auto b = test::renderEngine (e, res, src, ratio, 1.0, 8000, 1);

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));
    REQUIRE (maxDiff < 1.0e-4f);
}

// (f) ボイス状態独立: 2インスタンスを交互に回しても単独時と一致（規約9）
TEST_CASE ("GranularEngine instances are independent", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 0.5);
    const double ratio = std::pow (2.0, 7.0 / 12.0);

    GranularEngine solo;
    auto ref = test::renderEngine (solo, res, src, ratio, 1.0, 8000, 256);

    GranularEngine e1, e2;
    SourceReader r1, r2;
    r1.configure (&src, 0, src.numSamples, false);
    r2.configure (&src, 0, src.numSamples, false);
    PitchEngineContext ctx { 48000.0, 256, 1 };
    e1.prepare (ctx, res); e1.reset();
    e2.prepare (ctx, res); e2.reset();

    std::vector<float> o1 (8000, 0.0f), junk (256, 0.0f);
    std::vector<float> ratioBuf (256, (float) ratio);
    double p1 = 0.0, p2 = 0.0;
    for (int pos = 0; pos < 8000; pos += 256)
    {
        const int nn = std::min (256, 8000 - pos);
        float* a[1] = { o1.data() + pos };
        float* b[1] = { junk.data() };
        e1.process (r1, p1, a, 1, nn, ratioBuf.data(), 1.0);
        e2.process (r2, p2, b, 1, nn, ratioBuf.data(), 1.0);   // 間に別インスタンスを挟む
    }

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < ref.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (ref[i] - o1[i]));
    REQUIRE (maxDiff < 1.0e-6f);
}

// (g) n==1 で呼ばれても壊れない（規約7）
TEST_CASE ("GranularEngine works when called with n == 1", "[granular]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 0.3);
    GranularEngine e;

    auto out = test::renderEngine (e, res, src, std::pow (2.0, 3.0 / 12.0), 1.0, 6000, 1);
    REQUIRE (test::nonZeroExtent (out) > 3000);
}
