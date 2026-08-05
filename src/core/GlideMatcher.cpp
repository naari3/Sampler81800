#include "GlideMatcher.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace otomad
{

std::vector<int> computeGlideMatching (const std::vector<float>& oldPitches,
                                       const std::vector<float>& newPitches)
{
    const int m = (int) oldPitches.size();
    const int q = (int) newPitches.size();
    std::vector<int> result ((std::size_t) q, -1);
    if (m == 0 || q == 0)
        return result;

    // 昇順の並び順（元インデックスを保持）
    std::vector<int> os ((std::size_t) m), ns ((std::size_t) q);
    std::iota (os.begin(), os.end(), 0);
    std::iota (ns.begin(), ns.end(), 0);
    std::sort (os.begin(), os.end(), [&] (int a, int b) { return oldPitches[(std::size_t) a] < oldPitches[(std::size_t) b]; });
    std::sort (ns.begin(), ns.end(), [&] (int a, int b) { return newPitches[(std::size_t) a] < newPitches[(std::size_t) b]; });

    // dp[i][j] : old[i..), new[j..) の (最大対応数, 最小コスト)。
    // 目的: まず対応数を最大化、同点ならコスト最小（交差しない=順序保存DP）。
    struct Cell { int matches; float cost; };
    const float BIG = 1.0e30f;

    std::vector<std::vector<Cell>> dp ((std::size_t) (m + 1),
                                       std::vector<Cell> ((std::size_t) (q + 1), Cell { 0, 0.0f }));
    // 選択の復元用: 0=skip old, 1=skip new, 2=match
    std::vector<std::vector<int>> choice ((std::size_t) (m + 1),
                                          std::vector<int> ((std::size_t) (q + 1), -1));

    auto better = [] (Cell a, Cell b) {
        if (a.matches != b.matches) return a.matches > b.matches;   // 対応数優先
        return a.cost < b.cost;                                     // 同点はコスト最小
    };

    for (int i = m; i >= 0; --i)
    {
        for (int j = q; j >= 0; --j)
        {
            if (i == m && j == q) { dp[(std::size_t) i][(std::size_t) j] = { 0, 0.0f }; continue; }

            Cell best { -1, BIG };
            int  bestChoice = -1;

            if (i < m)   // old[i] をスキップ
            {
                Cell c = dp[(std::size_t) (i + 1)][(std::size_t) j];
                if (better (c, best)) { best = c; bestChoice = 0; }
            }
            if (j < q)   // new[j] をスキップ
            {
                Cell c = dp[(std::size_t) i][(std::size_t) (j + 1)];
                if (better (c, best)) { best = c; bestChoice = 1; }
            }
            if (i < m && j < q)   // 対応させる
            {
                const float d = std::abs (oldPitches[(std::size_t) os[(std::size_t) i]]
                                        - newPitches[(std::size_t) ns[(std::size_t) j]]);
                Cell nxt = dp[(std::size_t) (i + 1)][(std::size_t) (j + 1)];
                Cell c { nxt.matches + 1, nxt.cost + d };
                if (better (c, best)) { best = c; bestChoice = 2; }
            }

            dp[(std::size_t) i][(std::size_t) j]     = best;
            choice[(std::size_t) i][(std::size_t) j] = bestChoice;
        }
    }

    // 復元
    int i = 0, j = 0;
    while (i < m && j < q)
    {
        const int c = choice[(std::size_t) i][(std::size_t) j];
        if (c == 0)      { ++i; }
        else if (c == 1) { ++j; }
        else             // match: new ns[j] → old os[i]
        {
            result[(std::size_t) ns[(std::size_t) j]] = os[(std::size_t) i];
            ++i; ++j;
        }
    }
    return result;
}

} // namespace otomad
