#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "core/PortamentoGenerator.h"

using namespace otomad;
using Catch::Approx;

// portaTime=1000ms, curve=0 で 0->12半音を滑らせ、500ms地点が +6半音 ±5cent — 受け入れ条件(a)
TEST_CASE ("PortamentoGenerator reaches midpoint at half time (linear curve)", "[porta]")
{
    const double sr = 48000.0;
    PortamentoGenerator g;
    g.setSampleRate (sr);
    g.setShape (PortamentoGenerator::Shape::Time);
    g.setTime (1000.0f);
    g.setCurve (0.0f);
    g.startGlide (0.0f, 12.0f);

    const int half = (int) (0.5 * sr);   // 500ms
    std::vector<float> buf ((std::size_t) half, 0.0f);
    g.process (buf.data(), half);

    // 5 cent = 0.05 半音
    REQUIRE ((double) buf.back() == Approx (6.0).margin (0.05));
    REQUIRE (g.isGliding());
}

TEST_CASE ("PortamentoGenerator curve sign changes the shape", "[porta]")
{
    const double sr = 48000.0;
    const int half = (int) (0.5 * sr);

    auto midpointValue = [&] (float curve)
    {
        PortamentoGenerator g;
        g.setSampleRate (sr);
        g.setTime (1000.0f);
        g.setCurve (curve);
        g.startGlide (0.0f, 12.0f);
        std::vector<float> buf ((std::size_t) half, 0.0f);
        g.process (buf.data(), half);
        return buf.back();
    };

    // curve>0: 立ち上がりが速い → 中点で 6 より上、curve<0: 遅い → 6 より下
    REQUIRE (midpointValue (+1.0f) > 6.0f);
    REQUIRE (midpointValue (-1.0f) < 6.0f);
}

TEST_CASE ("PortamentoGenerator snaps and completes", "[porta]")
{
    PortamentoGenerator g;
    g.setSampleRate (48000.0);
    g.setTime (100.0f);

    SECTION ("startAt does not glide")
    {
        g.startAt (7.0f);
        REQUIRE_FALSE (g.isGliding());
        REQUIRE (g.nextSemitone() == Approx (7.0f));
    }
    SECTION ("glide finishes and holds target")
    {
        g.startGlide (0.0f, 4.0f);
        std::vector<float> buf (48000, 0.0f);   // 1s >> 100ms
        g.process (buf.data(), 48000);
        REQUIRE_FALSE (g.isGliding());
        REQUIRE (g.nextSemitone() == Approx (4.0f));
    }
}
