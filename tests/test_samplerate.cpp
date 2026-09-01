#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "core/PitchDetect.h"
#include "core/SampleLoader.h"

using namespace otomad;
using Catch::Approx;

// ============================================================================
// サンプルレートの回帰テスト。
//
// 再生側は data が「ホストSRに変換済み」である前提で読む（SR補正は一切しない）。
// なので data が別のSRのまま残ると、その比率がそのまま音程のズレになる。
//   44.1k で作った data を 48k で読む → 48000/44100 = +147 cent
//
// 実際に報告されたバグ:
//   setStateInformation は prepareToPlay より前に走るため、プロジェクトを開き直すと
//   hostSampleRate が既定値(44100)のまま data が作られ、その後 prepareToPlay が
//   48k を伝えても作り直されず、音程がズレたままになっていた。
// ============================================================================

namespace
{
constexpr double PI = 3.14159265358979;

// 指定SRの正弦波を original に持つ SampleBuffer を作る（data はまだ作らない）
SampleBuffer makeOriginal (double hz, double nativeSR, double seconds, int numCh = 1)
{
    SampleBuffer sb;
    sb.numChannels        = numCh;
    sb.originalSampleRate = nativeSR;
    const auto n = (std::size_t) (seconds * nativeSR);
    sb.original.assign ((std::size_t) numCh, std::vector<float> (n));
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto v = (float) (0.8 * std::sin (2.0 * PI * hz * (double) i / nativeSR));
        for (int ch = 0; ch < numCh; ++ch) sb.original[(std::size_t) ch][i] = v;
    }
    return sb;
}

// data を「playbackSR で再生されるもの」として周波数を測る
double measureDataHz (const SampleBuffer& sb, double playbackSR, double wantHz)
{
    const auto n = (int) std::min<std::int64_t> (sb.numSamples, 32768);
    REQUIRE (n > 4096);
    return test::estimateF0Near (sb.data[0].data() + 2048, n - 4096, playbackSR, wantHz);
}
}

TEST_CASE ("rebuildFromOriginal converts to the host sample rate", "[samplerate]")
{
    auto sb = makeOriginal (440.0, 44100.0, 1.0);
    SampleLoader::rebuildFromOriginal (sb, 48000.0);

    REQUIRE (sb.sampleRate == Approx (48000.0));
    // 長さは秒数が保たれるようにスケールされる
    REQUIRE ((double) sb.numSamples == Approx (48000.0).epsilon (0.01));
    REQUIRE (! sb.peaks.empty());   // 波形表示用のピークも作り直される
}

TEST_CASE ("a 44.1k buffer read at 48k is sharp by ~147 cents", "[samplerate]")
{
    // これが「直っていないときに起きること」。テストの前提を固定しておく。
    auto sb = makeOriginal (440.0, 44100.0, 1.0);
    SampleLoader::rebuildFromOriginal (sb, 44100.0);   // 44.1k のまま
    REQUIRE (sb.sampleRate == Approx (44100.0));

    // 48k で再生されたつもりで測る（＝再生側は data をホストSRとして読むのでこうなる）
    const double heard = measureDataHz (sb, 48000.0, 440.0 * 48000.0 / 44100.0);
    const double cents = 1200.0 * std::log2 (heard / 440.0);
    INFO ("heard = " << heard << " Hz, error = " << cents << " cent");
    REQUIRE (cents == Approx (147.0).margin (10.0));
}

TEST_CASE ("rebuilding for the host rate keeps the pitch", "[samplerate]")
{
    // 上と同じ素材でも、ホストSRへ作り直せば音程は保たれる
    auto sb = makeOriginal (440.0, 44100.0, 1.0);
    SampleLoader::rebuildFromOriginal (sb, 48000.0);

    const double heard = measureDataHz (sb, 48000.0, 440.0);
    const double cents = 1200.0 * std::log2 (heard / 440.0);
    INFO ("heard = " << heard << " Hz, error = " << cents << " cent");
    REQUIRE (std::abs (cents) < 10.0);
}

TEST_CASE ("rebuilding is done from original, never from data", "[samplerate]")
{
    // 規約16: data を再リサンプルすると劣化が累積する。何度SRを変えても
    // 常に original から1回だけ変換されるので、同じSRへ戻せば同じ結果になること。
    auto a = makeOriginal (440.0, 44100.0, 0.5);
    SampleLoader::rebuildFromOriginal (a, 48000.0);
    const auto direct = a.data[0];

    auto b = makeOriginal (440.0, 44100.0, 0.5);
    SampleLoader::rebuildFromOriginal (b, 96000.0);   // 一度べつのSRを経由してから
    SampleLoader::rebuildFromOriginal (b, 48000.0);

    REQUIRE (b.data[0].size() == direct.size());
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < direct.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (direct[i] - b.data[0][i]));
    INFO ("maxDiff = " << maxDiff);
    REQUIRE (maxDiff < 1.0e-6f);   // 経由の有無で結果が変わらない＝二重変換していない
}

TEST_CASE ("rebuildFromOriginal keeps stereo channels", "[samplerate]")
{
    auto sb = makeOriginal (330.0, 48000.0, 0.4, 2);
    SampleLoader::rebuildFromOriginal (sb, 44100.0);

    REQUIRE (sb.data.size() == 2);
    REQUIRE (sb.numChannels == 2);
    REQUIRE (sb.data[0].size() == sb.data[1].size());
    REQUIRE ((double) sb.numSamples == Approx (44100.0 * 0.4).epsilon (0.02));
}
