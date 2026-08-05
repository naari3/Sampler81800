#pragma once

#include <vector>

namespace otomad
{

//==============================================================================
/**
    ポリグライドのボイスマッチング。DESIGN.md §3.4 / §8.1。

    旧プール(oldPitches)と新グループ(newPitches)を、**昇順ソートして交差しない最適対応**で結ぶ。
    返り値 result[i] は newPitches[i] が滑る先の oldPitches のインデックス（対応なしは -1）。

    - 最大カーディナリティ（= min(|old|,|new|) 本を必ず対応）→ 同点なら総コスト（半音距離）最小。
    - **入力の順序に依存しない**（内部で昇順ソートするため）。これで到着順非依存(§8.1 (c))を保証する。
    - 同じ旧ノートが2つの新ノートに割り当たらない（順序保存マッチングの帰結）。

    値の単位は半音。O(n*m)、声部数はたかだか16なのでコストは無視できる。
*/
std::vector<int> computeGlideMatching (const std::vector<float>& oldPitches,
                                       const std::vector<float>& newPitches);

} // namespace otomad
