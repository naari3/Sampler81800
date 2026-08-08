#pragma once

#include <algorithm>
#include <cmath>

namespace otomad
{

//==============================================================================
/**
    ビブラート用の LFO。発音から delay 経過後、fade をかけて depth へ到達する正弦変調。

    出力は「半音」単位。呼び出し側はこれをピッチの半音値に加算してから比へ変換すること
    （規約4: ピッチは対数ドメインで補間する）。

    状態（位相・経過サンプル）はこのオブジェクトが持つ。ボイスごとに1つ持たせること（規約9）。
    JUCE 非依存にしてあるのでユニットテストから直接叩ける。
*/
class VibratoLfo
{
public:
    struct Config
    {
        float  depthSemi    = 0.0f;   // 振幅（半音）
        float  rateHz       = 5.0f;
        double delaySamples = 0.0;    // 効き始めるまで
        double fadeSamples  = 0.0;    // 最大振幅に達するまで
    };

    void prepare (double sr) noexcept
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        reset (0.0);
    }

    /** 発音時に呼ぶ。startElapsed に負値を渡すと、その分だけ開始が後ろへずれる
        （エンジンのレイテンシと Delay/Fade の基準を揃えるため）。 */
    void reset (double startElapsed) noexcept
    {
        phase   = 0.0;
        elapsed = startElapsed;
    }

    /** 1サンプル進めて変調量（半音）を返す。 */
    float next (const Config& c) noexcept
    {
        float mod = 0.0f;
        const double t = elapsed - c.delaySamples;
        if (t > 0.0 && c.depthSemi != 0.0f)
        {
            // fade=0 なら即フルデプス。それ以外は 0→1 に線形に立ち上げる。
            const float amt = c.fadeSamples > 0.0
                                ? (float) std::min (1.0, t / c.fadeSamples) : 1.0f;
            mod = amt * c.depthSemi * (float) std::sin (phase);
        }

        phase += twoPi * (double) c.rateHz / sampleRate;
        if (phase > twoPi)          // 位相は畳んで long note でも精度を保つ
            phase -= twoPi;
        elapsed += 1.0;
        return mod;
    }

    double getPhase()   const noexcept { return phase; }
    double getElapsed() const noexcept { return elapsed; }

private:
    static constexpr double twoPi = 6.283185307179586476925286766559;

    double sampleRate = 44100.0;
    double phase      = 0.0;
    double elapsed    = 0.0;
};

} // namespace otomad
