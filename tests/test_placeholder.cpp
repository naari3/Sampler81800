#include <catch2/catch_test_macros.hpp>

// Phase 0 のビルド健全性チェック。
// 実際のDSPテスト（ピッチ精度・出力長・ブロック分割不変性・ボイス状態独立性）は
// Phase 1 以降で追加する（DESIGN.md §8.1）。
TEST_CASE ("build sanity", "[placeholder]")
{
    REQUIRE (1 + 1 == 2);
}
