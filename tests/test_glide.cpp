#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "core/GlideMatcher.h"

using namespace otomad;

namespace
{
// new[i] が滑る先の old のピッチ値（対応なしは NaN 相当で -999）を返す。
std::vector<float> matchedOldPitch (const std::vector<float>& oldP, const std::vector<float>& newP)
{
    auto idx = computeGlideMatching (oldP, newP);
    std::vector<float> out;
    for (int i : idx)
        out.push_back (i >= 0 ? oldP[(std::size_t) i] : -999.0f);
    return out;
}
} // namespace

// 昇順どうしで対応・交差なし — 受け入れ条件(b)
TEST_CASE ("GlideMatcher pairs ascending voices without crossing", "[glide]")
{
    std::vector<float> oldP { 60, 64, 67 };
    std::vector<float> newP { 65, 69, 72 };
    REQUIRE (matchedOldPitch (oldP, newP) == std::vector<float> { 60, 64, 67 });
}

// 到着順（昇順/降順/混在）を変えても最終マッチングは同一 — 受け入れ条件(c)
TEST_CASE ("GlideMatcher is independent of arrival order", "[glide]")
{
    std::vector<float> oldP { 60, 63 };

    // {62,64} をどの順で渡しても 60->62, 63->64
    std::vector<std::vector<float>> orders {
        { 62, 64 },
        { 64, 62 },
    };
    for (auto& nP : orders)
    {
        auto idx = computeGlideMatching (oldP, nP);
        // 62 は 60 から、64 は 63 から
        for (std::size_t i = 0; i < nP.size(); ++i)
        {
            const float from = idx[i] >= 0 ? oldP[(std::size_t) idx[i]] : -999.0f;
            if (nP[i] == 62.0f) REQUIRE (from == 60.0f);
            if (nP[i] == 64.0f) REQUIRE (from == 63.0f);
        }
    }
}

TEST_CASE ("GlideMatcher handles unequal counts", "[glide]")
{
    SECTION ("fewer new: only some glide, extra old ignored")
    {
        std::vector<float> oldP { 60, 64, 67 };
        std::vector<float> newP { 62, 65 };
        // 最大対応=2, 最小コスト: 62->60, 65->64
        REQUIRE (matchedOldPitch (oldP, newP) == std::vector<float> { 60, 64 });
    }
    SECTION ("fewer old: only one glides")
    {
        std::vector<float> oldP { 60 };
        std::vector<float> newP { 60, 64, 67 };
        auto out = matchedOldPitch (oldP, newP);
        // ちょうど1本だけ対応（60->60）、残りは無し
        int matched = (int) std::count_if (out.begin(), out.end(), [] (float f) { return f > -900.0f; });
        REQUIRE (matched == 1);
        REQUIRE (out[0] == 60.0f);       // 60 は 60 から
    }
    SECTION ("empty pool: no glide")
    {
        std::vector<float> oldP {};
        std::vector<float> newP { 60, 64, 67 };
        auto idx = computeGlideMatching (oldP, newP);
        for (int v : idx) REQUIRE (v == -1);
    }
}

// 同じ旧ノートが2つの新ノートに割り当たらない
TEST_CASE ("GlideMatcher never assigns one old note to two new notes", "[glide]")
{
    std::vector<float> oldP { 60, 62, 64, 65 };
    std::vector<float> newP { 61, 63, 66, 68 };
    auto idx = computeGlideMatching (oldP, newP);

    std::vector<int> used;
    for (int v : idx)
        if (v >= 0)
        {
            REQUIRE (std::find (used.begin(), used.end(), v) == used.end());
            used.push_back (v);
        }
}
