#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "test_helpers.h"
#include "core/SourceReader.h"

using namespace otomad;

// トリムが相対位置0にマップされ、終端で尽きる
TEST_CASE ("SourceReader maps trim start to relative zero and finishes at trim length", "[reader]")
{
    auto src = test::makeSine (440.0, 48000.0, 0.1);   // 4800 サンプル
    SourceReader r;
    r.configure (&src, 1000, 2000, /*snap*/ false);

    REQUIRE (r.getTrimStart() == 1000);
    REQUIRE (r.getTrimmedLength() == 1000);

    // 相対0 = 絶対1000
    REQUIRE (r.sampleAt (0, 0) == src.sampleAtRaw (0, 1000));
    REQUIRE (r.sampleAt (0, 999) == src.sampleAtRaw (0, 1999));

    // 範囲外は 0
    REQUIRE (r.sampleAt (0, -1) == 0.0f);
    REQUIRE (r.sampleAt (0, 1000) == 0.0f);

    REQUIRE_FALSE (r.isFinished (999.0));
    REQUIRE (r.isFinished (1000.0));
}

// 不正なトリム値でも破綻しない — 受け入れ条件(b)の一部
TEST_CASE ("SourceReader tolerates invalid trim values", "[reader]")
{
    auto src = test::makeSine (440.0, 48000.0, 0.1);
    SourceReader r;

    SECTION ("end <= start clamps to at least 1 sample")
    {
        r.configure (&src, 2000, 2000, false);
        REQUIRE (r.getTrimmedLength() >= 1);
    }
    SECTION ("out-of-range indices are clamped")
    {
        r.configure (&src, -100, 999999, false);
        REQUIRE (r.getTrimStart() == 0);
        REQUIRE (r.getTrimEnd() == src.numSamples);
    }
    SECTION ("null buffer is safe")
    {
        r.configure (nullptr, 0, 100, false);
        REQUIRE (r.getTrimmedLength() == 0);
        REQUIRE (r.sampleAt (0, 0) == 0.0f);
    }
}

// ゼロクロス吸着 — 受け入れ条件(c)
TEST_CASE ("SourceReader snaps trim start to a rising zero crossing", "[reader][zerocross]")
{
    auto src = test::makeSine (440.0, 48000.0, 0.1);

    SECTION ("snapped edge satisfies s[start-1]<=0 and s[start]>0")
    {
        // 適当な非ゼロクロス位置を要求
        const std::int64_t requested = 1234;
        SourceReader r;
        r.configure (&src, requested, 4000, /*snap*/ true);

        const std::int64_t start = r.getTrimStart();
        REQUIRE (start >= 1);
        REQUIRE (src.sampleAtRaw (0, start - 1) <= 0.0f);
        REQUIRE (src.sampleAtRaw (0, start)     >  0.0f);

        // 吸着は ±2ms(=96サンプル) 以内
        REQUIRE (std::llabs ((long long) (start - requested)) <= 96);
    }

    SECTION ("no zero crossing in window returns requested position")
    {
        // 直流っぽく全部正の素材を作る → 立ち上がりゼロクロスは存在しない
        SampleBuffer dc;
        dc.numChannels = 1;
        dc.numSamples  = 500;
        dc.sampleRate  = 48000.0;
        dc.data.assign (1, std::vector<float> (500, 0.5f));

        const std::int64_t pos = 250;
        const std::int64_t got = SourceReader::snapToRisingZeroCross (dc, 0, pos, 96);
        REQUIRE (got == pos);
    }
}
