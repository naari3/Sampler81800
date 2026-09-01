#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "test_engine_helpers.h"
#include "pitch/StretchLibEngine.h"

using namespace otomad;
using Catch::Approx;

namespace
{
EngineResources makeRes () { EngineResources r; r.prepare (48000.0); return r; }
const double tenCents = std::pow (2.0, 10.0 / 1200.0) - 1.0;

// このライブラリは presetDefault で 5760サンプル(120ms @48k) の STFT ブロックを使うため、
// 素材が尽きた後の減衰テールが長い。既定しきい値(1e-4)だとテールまで長さに数えてしまうので、
// 「鳴っている本体」を測れるようにピークの数%を下回ったところで切る。
constexpr float bodyThreshold = 0.05f;
}

// (a) ピッチ精度: +7半音
TEST_CASE ("StretchLibEngine shifts +7 semitones within 10 cents", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.5);
    StretchLibEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);
    auto out = test::renderEngine (e, res, src, ratio, 1.0, 48000, 256);

    const double f0 = test::estimateF0Near (out.data() + 12000, 24000, 48000.0, 440.0 * ratio);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (tenCents));
}

// (b) 出力長: timeRatio=1 で長さ保持
TEST_CASE ("StretchLibEngine preserves duration at timeRatio 1", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    StretchLibEngine e;

    auto out = test::renderEngine (e, res, src, std::pow (2.0, 7.0 / 12.0), 1.0, 48000 + 16384, 256);
    REQUIRE ((double) test::nonZeroExtent (out, bodyThreshold) == Approx (48000.0).epsilon (0.10));
}

// (c) 出力長: timeRatio=0.5 で約2倍、pitchRatio=1 なら F0 不変
TEST_CASE ("StretchLibEngine stretches to 2x at timeRatio 0.5", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    StretchLibEngine e;

    auto out = test::renderEngine (e, res, src, 1.0, 0.5, 96000 + 16384, 256);
    REQUIRE ((double) test::nonZeroExtent (out, bodyThreshold) == Approx (96000.0).epsilon (0.10));

    const double f0 = test::estimateF0Near (out.data() + 20000, 40000, 48000.0, 440.0);
    REQUIRE (f0 == Approx (440.0).epsilon (tenCents));
}

// (d) ピッチと長さが干渉しない（規約5）
TEST_CASE ("StretchLibEngine pitch and duration are independent", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    StretchLibEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);
    auto out = test::renderEngine (e, res, src, ratio, 0.5, 96000 + 16384, 256);

    REQUIRE ((double) test::nonZeroExtent (out, bodyThreshold) == Approx (96000.0).epsilon (0.10));
    const double f0 = test::estimateF0Near (out.data() + 20000, 40000, 48000.0, 440.0 * ratio);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (tenCents));
}

// (e) ブロック分割不変性（規約7）
TEST_CASE ("StretchLibEngine output is independent of block size", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (330.0, 48000.0, 0.5);
    StretchLibEngine e;

    const double ratio = std::pow (2.0, 5.0 / 12.0);
    auto a = test::renderEngine (e, res, src, ratio, 1.0, 8000, 256);
    auto b = test::renderEngine (e, res, src, ratio, 1.0, 8000, 64);

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));
    INFO ("maxDiff = " << maxDiff);
    REQUIRE (maxDiff < 1.0e-3f);
}

// (f) ボイス状態独立: 2インスタンスを交互に回しても単独時と一致（規約9）
TEST_CASE ("StretchLibEngine instances are independent", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 0.5);
    const double ratio = std::pow (2.0, 7.0 / 12.0);

    StretchLibEngine solo;
    auto ref = test::renderEngine (solo, res, src, ratio, 1.0, 8000, 256);

    StretchLibEngine e1, e2;
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
TEST_CASE ("StretchLibEngine works when called with n == 1", "[stretchlib]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 0.5);
    StretchLibEngine e;

    auto out = test::renderEngine (e, res, src, std::pow (2.0, 3.0 / 12.0), 1.0, 12000, 1);
    REQUIRE (test::nonZeroExtent (out, bodyThreshold) > 3000);
}
