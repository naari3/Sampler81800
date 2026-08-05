#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "core/VoiceAllocator.h"

using namespace otomad;

// 同一ノート連打でボイスが増殖しない — 受け入れ条件(d)
TEST_CASE ("VoiceAllocator reuses the same-note voice", "[alloc]")
{
    std::vector<VoiceSlotState> st (4);
    st[0] = { true, 60, 100, false };
    st[1] = { true, 64, 110, false };

    // 60 を再発音 → スロット0を再利用（新規確保しない）
    REQUIRE (chooseVoiceSlot (st, 60) == 0);
}

TEST_CASE ("VoiceAllocator prefers free voice, then steals oldest", "[alloc]")
{
    std::vector<VoiceSlotState> st (3);

    SECTION ("free voice chosen")
    {
        st[0] = { true, 60, 100, false };
        st[1] = { false, -1, 0, false };
        st[2] = { true, 64, 120, false };
        REQUIRE (chooseVoiceSlot (st, 67) == 1);
    }
    SECTION ("all busy: steal oldest onTime")
    {
        st[0] = { true, 60, 300, false };
        st[1] = { true, 62, 100, false };   // 最古
        st[2] = { true, 64, 200, false };
        REQUIRE (chooseVoiceSlot (st, 67) == 1);
    }
    SECTION ("all busy: releasing voice preferred over active oldest")
    {
        st[0] = { true, 60, 100, false };   // 最古だがアクティブ
        st[1] = { true, 62, 300, true  };   // リリース中
        st[2] = { true, 64, 200, false };
        REQUIRE (chooseVoiceSlot (st, 67) == 1);
    }
}
