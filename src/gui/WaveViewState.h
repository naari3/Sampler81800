#pragma once

#include <algorithm>

namespace otomad::gui
{

//==============================================================================
/**
    波形表示のズーム/スクロール窓（GUIスレッド専用の共有状態）。
    値はすべて「サンプル全体に対する割合(0..1)」。描画(WaveformView)と
    トリムのヒットテスト(DropZone)で同じ窓を共有するために外だしする。
*/
struct WaveViewState
{
    double start = 0.0;   // 表示左端（サンプル割合）
    double end   = 1.0;   // 表示右端（サンプル割合）

    double span() const noexcept { return end - start; }
    void   reset() noexcept { start = 0.0; end = 1.0; }

    // ピクセル比 fx(0..1, 左端=0) → サンプル位置(0..1)
    double toSample (double fx) const noexcept { return start + fx * (end - start); }
    // サンプル位置(0..1) → 表示上のピクセル比(0..1)
    double toView (double s) const noexcept
    { const double sp = end - start; return sp > 1.0e-9 ? (s - start) / sp : 0.0; }

    // マウス位置 fx を中心にズーム（factor<1 で拡大 / >1 で縮小）。窓は [0,1] 内に収める。
    void zoom (double fx, double factor) noexcept
    {
        const double anchor = toSample (fx);
        double sp = std::clamp ((end - start) * factor, kMinSpan, 1.0);
        double st = std::clamp (anchor - fx * sp, 0.0, 1.0 - sp);
        start = st; end = st + sp;
    }

    // 横スクロール（表示幅に対する割合ぶん平行移動）
    void pan (double dxFrac) noexcept
    {
        const double sp = end - start;
        const double st = std::clamp (start + dxFrac * sp, 0.0, 1.0 - sp);
        start = st; end = st + sp;
    }

    static constexpr double kMinSpan = 0.0005;   // 最大ズーム（全体の 1/2000）
};

} // namespace otomad::gui
