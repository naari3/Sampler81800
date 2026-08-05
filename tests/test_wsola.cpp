#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "test_engine_helpers.h"
#include "pitch/WsolaEngine.h"

using namespace otomad;
using Catch::Approx;

namespace
{
EngineResources makeRes () { EngineResources r; r.prepare (48000.0); return r; }
const double oneCent  = std::pow (2.0, 1.0 / 1200.0) - 1.0;
const double tenCents = std::pow (2.0, 10.0 / 1200.0) - 1.0;
}

// (a) +7半音で F0 が期待値 ±10cent
TEST_CASE ("WsolaEngine shifts +7 semitones within 10 cents", "[wsola]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.5);
    WsolaEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);   // ≈1.4983
    auto out = test::renderEngine (e, res, src, ratio, 1.0, 48000, 256);

    const double f0 = test::estimateF0 (out.data() + 6000, 36000, 48000.0);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (tenCents));
}

// (b) durationMode=Natural(timeRatio=1) で長さ保持: 出力の非ゼロ区間 ≈ 原音長 + tail(±ゆるめ)
TEST_CASE ("WsolaEngine preserves duration at timeRatio 1", "[wsola]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);   // 48000 サンプル
    WsolaEngine e;

    auto out = test::renderEngine (e, res, src, std::pow (2.0, 7.0 / 12.0), 1.0, 48000 + 8192, 256);
    const int ext = test::nonZeroExtent (out);

    // 原音長 ± (tail=frame 2048 + 立ち上がり) 程度。ここは ±5% で担保。
    REQUIRE ((double) ext == Approx (48000.0).epsilon (0.05));
}

// (c) timeRatio=0.5 で出力長が約2倍、かつ F0 は原音のまま（pitchRatio=1）
TEST_CASE ("WsolaEngine stretches to 2x at timeRatio 0.5, pitch unchanged", "[wsola]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    WsolaEngine e;

    auto out = test::renderEngine (e, res, src, 1.0, 0.5, 96000 + 8192, 256);
    const int ext = test::nonZeroExtent (out);
    REQUIRE ((double) ext == Approx (96000.0).epsilon (0.05));

    const double f0 = test::estimateF0 (out.data() + 6000, 80000, 48000.0);
    REQUIRE (f0 == Approx (440.0).epsilon (tenCents));
}

// (d) ピッチと長さが互いに干渉しない: +7半音 × 2倍長 の同時指定で両方成立
TEST_CASE ("WsolaEngine pitch and duration are independent", "[wsola]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 1.0);
    WsolaEngine e;

    const double ratio = std::pow (2.0, 7.0 / 12.0);
    auto out = test::renderEngine (e, res, src, ratio, 0.5, 96000 + 8192, 256);   // pitch+7, 長さ2倍

    REQUIRE ((double) test::nonZeroExtent (out) == Approx (96000.0).epsilon (0.05));   // 2倍長
    const double f0 = test::estimateF0 (out.data() + 6000, 80000, 48000.0);
    REQUIRE (f0 == Approx (440.0 * ratio).epsilon (tenCents));                          // +7半音
}

// (e) ブロック分割不変性: blockSize 256 と 1 で出力一致
TEST_CASE ("WsolaEngine output is independent of block size", "[wsola]")
{
    auto res = makeRes();
    auto src = test::makeSine (330.0, 48000.0, 0.5);
    WsolaEngine e;

    const double ratio = std::pow (2.0, 5.0 / 12.0);
    auto a = test::renderEngine (e, res, src, ratio, 1.0, 8000, 256);
    auto b = test::renderEngine (e, res, src, ratio, 1.0, 8000, 1);

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));
    REQUIRE (maxDiff < 1.0e-4f);
}

// (h) ボイス状態独立: 2インスタンスが混信しない（別々に回して単独と一致）
TEST_CASE ("WsolaEngine instances are independent", "[wsola]")
{
    auto res = makeRes();
    auto src = test::makeSine (440.0, 48000.0, 0.5);
    const double ratio = std::pow (2.0, 7.0 / 12.0);

    WsolaEngine solo;
    auto ref = test::renderEngine (solo, res, src, ratio, 1.0, 8000, 256);

    // 2つ同時に、間にもう一方の process を挟んでも各自の出力は変わらない
    WsolaEngine e1, e2;
    SourceReader r1, r2;
    r1.configure (&src, 0, src.numSamples, false);
    r2.configure (&src, 0, src.numSamples, false);
    PitchEngineContext ctx { 48000.0, 256, 1 };
    e1.prepare (ctx, res); e1.reset();
    e2.prepare (ctx, res); e2.reset();

    std::vector<float> o1 (8000, 0.0f);
    std::vector<float> ratioBuf (256, (float) ratio);
    double p1 = 0.0, p2 = 0.0;
    std::vector<float> junk (256, 0.0f);
    for (int pos = 0; pos < 8000; pos += 256)
    {
        const int nn = std::min (256, 8000 - pos);
        float* ptr = o1.data() + pos; float* ptrs[1] = { ptr };
        e1.process (r1, p1, ptrs, 1, nn, ratioBuf.data(), 1.0);
        float* jp[1] = { junk.data() };
        e2.process (r2, p2, jp, 1, nn, ratioBuf.data(), 1.0);   // 混信誘発
    }
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < o1.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (o1[i] - ref[i]));
    REQUIRE (maxDiff < 1.0e-4f);
}
