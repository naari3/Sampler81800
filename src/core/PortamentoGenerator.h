#pragma once

namespace otomad
{

//==============================================================================
/**
    ポルタメント（グライド）。DESIGN.md §3.3。
    **ピッチは常に半音（対数）ドメインで補間する**（規約4）。値の単位は半音（絶対値でよい）。

    Mode（Off/Legato/Always）の判定は VoiceManager 側が行い、ここには「滑らせるか/スナップか」
    だけが渡る。ポリグライドのグループ内オリジン再割当（§3.4）のため setOrigin を持つ。
*/
class PortamentoGenerator
{
public:
    enum class Shape { Time, Analog };

    void setSampleRate (double sr) noexcept;
    void setTime (float ms) noexcept;        // portaTime。Time=総時間 / Analog=99%到達時間
    void setCurve (float c) noexcept;        // -1..1（Timeモードのみ）
    void setShape (Shape s) noexcept;

    void startAt (float semi) noexcept;                    // スナップ（グライドなし）
    void startGlide (float origin, float target) noexcept; // origin→target を滑る
    void setOrigin (float origin) noexcept;                // オリジン再割当（progress≈0で使う, §3.4）
    void setTarget (float target) noexcept;                // 現在値から新ターゲットへ（mono last-note）
    void snap () noexcept;                                  // 即ターゲットへ

    float current () const noexcept  { return cur; }
    float progress () const noexcept;
    bool  isGliding () const noexcept { return gliding; }

    float nextSemitone () noexcept;
    void  process (float* dst, int n) noexcept;

private:
    void recalcAnalog () noexcept;
    bool differs (float a, float b) const noexcept;

    double sr           = 44100.0;
    float  totalMs      = 80.0f;
    double totalSamples = 0.0;
    float  curve        = 0.0f;
    Shape  shape        = Shape::Time;

    float  startS  = 0.0f;
    float  targetS = 0.0f;
    float  cur     = 0.0f;
    double elapsed = 0.0;
    bool   gliding = false;
    float  analogCoeff = 0.0f;
};

} // namespace otomad
