#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "core/SourceReader.h"
#include "pitch/VarispeedEngine.h"

using namespace otomad;
using Catch::Approx;

namespace
{
// ratio 一定で n サンプルをレンダリングして 1ch 出力を返す。
std::vector<float> renderConstRatio (VarispeedEngine::Quality q,
                                     const SampleBuffer& src, double ratio, int n)
{
    SourceReader reader;
    reader.configure (&src, 0, src.numSamples, /*snap*/ false);

    VarispeedEngine engine;
    engine.setQuality (q);

    std::vector<float> out ((std::size_t) n, 0.0f);
    std::vector<float> ratioBuf ((std::size_t) n, (float) ratio);
    float* ptrs[1] = { out.data() };

    double srcPos = 0.0;
    engine.process (reader, srcPos, ptrs, 1, n, ratioBuf.data());
    return out;
}
} // namespace

// +12半音で F0 が2倍 (880Hz ±1cent) — 受け入れ条件(a)
TEST_CASE ("VarispeedEngine shifts +12 semitones to 880Hz within 1 cent", "[varispeed]")
{
    const double sr = 48000.0;
    const auto   src = test::makeSine (440.0, sr, 1.0);

    // 1 cent 相対誤差
    const double oneCent = std::pow (2.0, 1.0 / 1200.0) - 1.0;   // ≈ 5.78e-4

    const auto quality = GENERATE (VarispeedEngine::Quality::Linear,
                                   VarispeedEngine::Quality::Hermite);

    const int n = 19200;   // 0.4s 出力（ratio 2 でも素材内に収まる）
    const auto out = renderConstRatio (quality, src, 2.0, n);

    // 端の過渡を避けて中央で推定
    const double f0 = test::estimateF0 (out.data() + 1000, n - 2000, sr);
    REQUIRE (f0 == Approx (880.0).epsilon (oneCent));
}

// ratio 1.0 / 0.5 の基本周波数
TEST_CASE ("VarispeedEngine ratio 1.0 and 0.5 fundamentals", "[varispeed]")
{
    const double sr = 48000.0;
    const auto   src = test::makeSine (440.0, sr, 1.0);
    const double oneCent = std::pow (2.0, 1.0 / 1200.0) - 1.0;

    SECTION ("ratio 1.0 -> 440Hz")
    {
        const auto out = renderConstRatio (VarispeedEngine::Quality::Hermite, src, 1.0, 24000);
        const double f0 = test::estimateF0 (out.data() + 1000, 22000, sr);
        REQUIRE (f0 == Approx (440.0).epsilon (oneCent));
    }
    SECTION ("ratio 0.5 -> 220Hz")
    {
        const auto out = renderConstRatio (VarispeedEngine::Quality::Hermite, src, 0.5, 24000);
        const double f0 = test::estimateF0 (out.data() + 1000, 22000, sr);
        REQUIRE (f0 == Approx (220.0).epsilon (oneCent));
    }
}

// 素材終端を超えると 0 を返す（NaN/Inf なし）
TEST_CASE ("VarispeedEngine returns silence past end of source", "[varispeed]")
{
    const double sr = 48000.0;
    const auto   src = test::makeSine (440.0, sr, 0.01);   // 480 サンプル

    // ratio 2 で 2000 サンプル要求 → 大半が範囲外
    const auto out = renderConstRatio (VarispeedEngine::Quality::Hermite, src, 2.0, 2000);

    for (float v : out)
        REQUIRE (std::isfinite (v));

    // 終端以降は無音
    REQUIRE (out.back() == Approx (0.0f).margin (1.0e-6f));
}
