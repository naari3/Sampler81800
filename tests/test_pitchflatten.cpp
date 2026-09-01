#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "core/PitchDetect.h"
#include "core/PitchFlattener.h"

using namespace otomad;
using Catch::Approx;

namespace
{
constexpr double SR = 48000.0;

// 音程が時間で揺れる「喋り声っぽい」信号を作る（基音＋倍音、位相連続）。
// midiAt(t) が MIDI ノート番号を返す。
template <typename F>
std::vector<std::vector<float>> makeGlidingTone (double seconds, F midiAt, int numCh = 1)
{
    const auto n = (std::size_t) (seconds * SR);
    std::vector<float> mono (n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t  = (double) i / SR;
        const double hz = pitchdetect::midiToHz (midiAt (t));
        phase += 2.0 * 3.14159265358979 * hz / SR;
        // 倍音を足して YIN が拾いやすい（＝声らしい）スペクトルにする
        mono[i] = (float) (0.5 * std::sin (phase) + 0.2 * std::sin (2.0 * phase)
                                                  + 0.1 * std::sin (3.0 * phase));
    }
    return std::vector<std::vector<float>> ((std::size_t) numCh, mono);
}

// 区間の実効ピッチ（MIDI）を YIN で測る
double measureMidi (const std::vector<float>& x, std::size_t from, std::size_t len)
{
    std::vector<float> w (x.begin() + (long) from, x.begin() + (long) (from + len));
    double mean = 0.0;
    for (float v : w) mean += v;
    mean /= (double) w.size();
    for (auto& v : w) v -= (float) mean;

    double hz = 0.0;
    const double aper = pitchdetect::yinWindow (w.data(), (int) w.size(), SR, hz);
    return (aper < 0.3 && hz > 0.0) ? pitchdetect::hzToMidi (hz) : 0.0;
}
}

//==============================================================================
TEST_CASE ("PitchDetect yinWindow finds a pure tone within 5 cents", "[flatten]")
{
    auto src = test::makeSine (220.0, SR, 0.5);
    std::vector<float> w (src.data[0].begin(), src.data[0].begin() + 4096);

    double hz = 0.0;
    const double aper = pitchdetect::yinWindow (w.data(), (int) w.size(), SR, hz);
    REQUIRE (aper < 0.2);
    REQUIRE (hz == Approx (220.0).epsilon (std::pow (2.0, 5.0 / 1200.0) - 1.0));
}

TEST_CASE ("flatten detects the median pitch and snaps to the nearest semitone", "[flatten]")
{
    // A3(57) を中心に ±1半音で 3Hz 揺らす → 中央値は 57 付近、スナップ先も 57
    auto in = makeGlidingTone (1.5, [] (double t) { return 57.0 + std::sin (2.0 * 3.14159 * 3.0 * t); });
    const auto n = (std::int64_t) in[0].size();

    auto r = flattenToSinglePitch (in, 1, n, SR, 0, n);
    REQUIRE (r.ok);
    REQUIRE (r.targetNote == 57);
    REQUIRE (r.detectedMidi == Approx (57.0).margin (0.5));
    REQUIRE (r.voicedFrames > 50);
}

TEST_CASE ("flatten removes the pitch wobble", "[flatten]")
{
    // ±1半音（=±100cent）で揺れる素材。平坦化後は目標から数十cent以内に収まること。
    auto in = makeGlidingTone (1.5, [] (double t) { return 57.0 + std::sin (2.0 * 3.14159 * 3.0 * t); });
    const auto n = (std::int64_t) in[0].size();

    auto r = flattenToSinglePitch (in, 1, n, SR, 0, n);
    REQUIRE (r.ok);
    REQUIRE (r.audio.size() == 1);
    REQUIRE ((std::int64_t) r.audio[0].size() == n);   // 長さが変わらないこと

    // 揺れの山と谷にあたる時刻で測って、どちらも目標付近にいることを確かめる。
    // 元素材ならここは ±100cent 振れている。
    const double target = 57.0;
    double worst = 0.0;
    for (double t : { 0.35, 0.50, 0.68, 0.85, 1.02, 1.18 })
    {
        const auto at = (std::size_t) (t * SR);
        const double m = measureMidi (r.audio[0], at, 4096);
        REQUIRE (m > 0.0);
        worst = std::max (worst, std::abs (m - target));
    }
    INFO ("worst deviation (semitones) = " << worst);
    REQUIRE (worst < 0.15);   // 15 cent 以内
}

// 補正カーブとエンジンの読み出し位置のアラインメント回帰テスト。
// ratio を合成ホップぶん前倒ししないと補正が半分ほどしか効かず、うねりとして残る。
// ここでは「入力の揺れに対して出力の揺れが十分小さいこと」を比で見る。
TEST_CASE ("flatten correction is aligned with the audio", "[flatten]")
{
    auto in = makeGlidingTone (1.6, [] (double t) { return 57.0 + std::sin (2.0 * 3.14159 * 3.0 * t); });
    const auto n = (std::int64_t) in[0].size();

    auto r = flattenToSinglePitch (in, 1, n, SR, 0, n);
    REQUIRE (r.ok);

    // 入力の揺れ幅（±100cent = 1半音）に対し、出力の揺れ幅を測る
    auto spread = [] (const std::vector<float>& x)
    {
        double lo = 1e9, hi = -1e9;
        for (double t : { 0.40, 0.48, 0.56, 0.64, 0.72, 0.80, 0.88, 0.96, 1.04, 1.12 })
        {
            const double m = measureMidi (x, (std::size_t) (t * SR), 4096);
            if (m <= 0.0) continue;
            lo = std::min (lo, m); hi = std::max (hi, m);
        }
        return (hi > lo) ? hi - lo : 0.0;
    };

    const double inSpread  = spread (in[0]);
    const double outSpread = spread (r.audio[0]);
    INFO ("in spread = " << inSpread << " semitones, out spread = " << outSpread);
    REQUIRE (inSpread > 1.0);            // 素材はちゃんと揺れている
    REQUIRE (outSpread < inSpread / 5);  // 揺れが 1/5 以下に潰れていること
}

TEST_CASE ("flatten strength 0 leaves the pitch contour alone", "[flatten]")
{
    auto in = makeGlidingTone (1.2, [] (double t) { return 57.0 + std::sin (2.0 * 3.14159 * 3.0 * t); });
    const auto n = (std::int64_t) in[0].size();

    FlattenOptions opt;
    opt.strength = 0.0f;
    auto r = flattenToSinglePitch (in, 1, n, SR, 0, n, opt);
    REQUIRE (r.ok);

    // strength=0 なら補正量は 0 → 元の揺れが残る
    const double a = measureMidi (r.audio[0], (std::size_t) (0.35 * SR), 4096);
    const double b = measureMidi (r.audio[0], (std::size_t) (0.52 * SR), 4096);
    REQUIRE (a > 0.0);
    REQUIRE (b > 0.0);
    REQUIRE (std::abs (a - b) > 0.4);   // まだ大きく動いている
}

TEST_CASE ("flatten is a no-op result on material with no clear pitch", "[flatten]")
{
    // ホワイトノイズ: 明確な音程が無いので ok=false（音を壊さない）
    std::vector<float> noise ((std::size_t) (SR * 0.6));
    unsigned int seed = 12345;
    for (auto& v : noise)
    {
        seed = seed * 1103515245u + 12345u;
        v = (float) ((int) ((seed >> 16) & 0x7fff) - 16384) / 16384.0f * 0.5f;
    }
    std::vector<std::vector<float>> in { noise };

    auto r = flattenToSinglePitch (in, 1, (std::int64_t) noise.size(), SR, 0, (std::int64_t) noise.size());
    REQUIRE_FALSE (r.ok);
    REQUIRE (r.audio.empty());
}

TEST_CASE ("flatten keeps stereo channel count and length", "[flatten]")
{
    auto in = makeGlidingTone (0.8, [] (double t) { return 60.0 + 0.5 * std::sin (2.0 * 3.14159 * 4.0 * t); }, 2);
    const auto n = (std::int64_t) in[0].size();

    auto r = flattenToSinglePitch (in, 2, n, SR, 0, n);
    REQUIRE (r.ok);
    REQUIRE (r.audio.size() == 2);
    REQUIRE ((std::int64_t) r.audio[0].size() == n);
    REQUIRE ((std::int64_t) r.audio[1].size() == n);
}

TEST_CASE ("analyseOnly produces a contour without rendering audio", "[flatten]")
{
    auto in = makeGlidingTone (1.0, [] (double) { return 60.0; });
    const auto n = (std::int64_t) in[0].size();

    auto r = analyseOnly (in, 1, n, SR, 0, n);
    REQUIRE (r.ok);
    REQUIRE (r.audio.empty());
    REQUIRE (r.targetNote == 60);
    REQUIRE (r.contour.midi.size() > 50);
    REQUIRE (r.contour.hopSeconds == Approx (0.010).margin (0.001));
}
