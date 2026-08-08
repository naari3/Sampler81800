// ビブラート LFO のテスト。
// Voice::render のピッチ計算はすべてのエンジンが通る経路なので、
// 変調そのものの性質（無効時ゼロ / 深さ・速さの精度 / delay・fade / ブロック分割不変性 /
// ボイス独立性）をここで押さえる。規約#18 の趣旨に沿う。
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "core/VibratoLfo.h"

using otomad::VibratoLfo;

namespace
{
    constexpr double kSr = 48000.0;

    VibratoLfo::Config cfg (float depthSemi, float rateHz, double delayMs, double fadeMs)
    {
        VibratoLfo::Config c;
        c.depthSemi    = depthSemi;
        c.rateHz       = rateHz;
        c.delaySamples = delayMs * 0.001 * kSr;
        c.fadeSamples  = fadeMs  * 0.001 * kSr;
        return c;
    }

    std::vector<float> run (VibratoLfo& lfo, const VibratoLfo::Config& c, int n)
    {
        std::vector<float> out ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            out[(std::size_t) i] = lfo.next (c);
        return out;
    }
}

TEST_CASE ("VibratoLfo is silent when depth is zero")
{
    VibratoLfo lfo; lfo.prepare (kSr); lfo.reset (0.0);
    const auto out = run (lfo, cfg (0.0f, 5.0f, 0.0, 0.0), 4800);

    for (float v : out)
        REQUIRE (v == 0.0f);   // 無効時は一切ピッチに影響しないこと（回帰ガード）
}

TEST_CASE ("VibratoLfo reaches the requested depth after the fade")
{
    // depth 0.5 半音(=50セント), rate 5Hz, delay 0, fade 100ms
    const auto c = cfg (0.5f, 5.0f, 0.0, 100.0);
    VibratoLfo lfo; lfo.prepare (kSr); lfo.reset (0.0);

    const auto out = run (lfo, c, (int) (1.0 * kSr));   // 1秒

    // fade 完了後（0.2s 以降）のピークが depth に一致するはず
    float peak = 0.0f;
    for (std::size_t i = (std::size_t) (0.2 * kSr); i < out.size(); ++i)
        peak = std::max (peak, std::abs (out[i]));

    REQUIRE_THAT (peak, Catch::Matchers::WithinAbs (0.5f, 0.01f));
}

TEST_CASE ("VibratoLfo modulates at the requested rate")
{
    const float rate = 6.0f;
    const auto c = cfg (1.0f, rate, 0.0, 0.0);   // fade なしで即フルデプス
    VibratoLfo lfo; lfo.prepare (kSr); lfo.reset (0.0);

    const auto out = run (lfo, c, (int) (2.0 * kSr));

    // 上向きゼロクロスの回数から周波数を測る
    int crossings = 0;
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i - 1] <= 0.0f && out[i] > 0.0f)
            ++crossings;

    const double measured = crossings / 2.0;   // 2秒ぶん
    REQUIRE_THAT (measured, Catch::Matchers::WithinAbs ((double) rate, 0.6));
}

TEST_CASE ("VibratoLfo stays silent during the delay and ramps during the fade")
{
    const auto c = cfg (1.0f, 10.0f, 100.0, 200.0);   // delay 100ms, fade 200ms
    VibratoLfo lfo; lfo.prepare (kSr); lfo.reset (0.0);
    const auto out = run (lfo, c, (int) (0.5 * kSr));

    // delay 中は完全に無変調
    for (std::size_t i = 0; i < (std::size_t) (0.1 * kSr); ++i)
        REQUIRE (out[i] == 0.0f);

    auto peakIn = [&] (double t0, double t1)
    {
        float p = 0.0f;
        for (auto i = (std::size_t) (t0 * kSr); i < (std::size_t) (t1 * kSr) && i < out.size(); ++i)
            p = std::max (p, std::abs (out[i]));
        return p;
    };

    // fade 途中 < fade 完了後、という単調な立ち上がりになっていること
    const float early = peakIn (0.10, 0.18);   // fade 序盤
    const float late  = peakIn (0.30, 0.50);   // fade 完了後
    REQUIRE (early < late);
    REQUIRE_THAT (late, Catch::Matchers::WithinAbs (1.0f, 0.02f));
}

TEST_CASE ("VibratoLfo output is independent of block splitting")
{
    const auto c = cfg (0.4f, 7.0f, 20.0, 80.0);

    VibratoLfo a; a.prepare (kSr); a.reset (0.0);
    const auto whole = run (a, c, 4096);

    // 同じ総サンプル数を細かく分けて生成しても、1サンプルずつ進む以上は完全一致するはず。
    // （将来「ブロック先頭で1回だけ更新」に書き換えられたらここで落ちる）
    VibratoLfo b; b.prepare (kSr); b.reset (0.0);
    std::vector<float> split;
    for (const int chunk : { 1, 7, 64, 333, 1024, 2667 })
        for (int i = 0; i < chunk && (int) split.size() < 4096; ++i)
            split.push_back (b.next (c));
    while ((int) split.size() < 4096)
        split.push_back (b.next (c));

    REQUIRE (split.size() == whole.size());
    for (std::size_t i = 0; i < whole.size(); ++i)
        REQUIRE (split[i] == whole[i]);
}

TEST_CASE ("VibratoLfo instances are independent per voice")
{
    const auto c = cfg (0.5f, 5.0f, 0.0, 0.0);

    // 規約9: 位相・経過はボイス固有。開始タイミングがずれれば出力もずれる。
    VibratoLfo v1; v1.prepare (kSr); v1.reset (0.0);
    VibratoLfo v2; v2.prepare (kSr); v2.reset (0.0);

    run (v1, c, 512);            // v1 だけ先に進める
    const auto a = run (v1, c, 256);
    const auto b = run (v2, c, 256);

    bool differs = false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-6f) { differs = true; break; }

    REQUIRE (differs);
}

TEST_CASE ("VibratoLfo negative start delays onset by the engine latency")
{
    // レイテンシ分だけ負から始めると、その分だけ Delay の起点が後ろへずれる
    const int lat = 2048;
    const auto c = cfg (1.0f, 5.0f, 0.0, 0.0);

    VibratoLfo lfo; lfo.prepare (kSr); lfo.reset (-(double) lat);
    const auto out = run (lfo, c, lat + 480);

    for (int i = 0; i < lat; ++i)
        REQUIRE (out[(std::size_t) i] == 0.0f);   // 音が出るまでは揺れない

    float after = 0.0f;
    for (std::size_t i = (std::size_t) lat; i < out.size(); ++i)
        after = std::max (after, std::abs (out[i]));
    REQUIRE (after > 0.0f);
}
