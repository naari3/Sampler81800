#include <catch2/catch_test_macros.hpp>

#include "pitch/ElastiqueDirect.h"
#include "pitch/PitchCache.h"

using namespace otomad;

// ============================================================================
// キャッシュは「作れない音程を要求しない」こと。
//
// élastique の DLL はピッチ factor が [0.25, 4.0] の範囲でしか動かず、
// それを外れると ProcessData が何も返さない＝出力が空になる。
// 以前は使用可能範囲を Pro -39..+48 / Soloist -17..+41 と申告していたため、
// プリウォームが ±48 半音を要求 → 半分近くが毎回空を返す → ready にならないので
// また要求される、を繰り返し、進捗バーが上がらないまま残った。
// （REAPER Shifter → WSOLA → REAPER Shifter と往復すると毎回この束が再投入される）
// ============================================================================

TEST_CASE ("elastique usable range matches the measured values", "[cache]")
{
    // 実測値（関門を -96..+96 に開けて DLL の素の挙動を測ったもの）。
    // ここを動かすと、作れない音程を延々と要求するか、作れる音程を捨てるかのどちらかになる。
    // **変えるなら必ず実機で測り直すこと。** 一度 ±24 に狭めて改悪した（原因は
    // プローブが古い ElastiqueDirect.obj を掴んでいて、DLL ではなく自分の関門を測っていた）。
    int lo = 0, hi = 0;

    ElastiqueDirect::usableSemitoneRange (ElastiqueDirect::Pro, lo, hi);
    REQUIRE (lo == -39);    // -40 以下は音程が +400〜500 cent ずれる
    REQUIRE (hi ==  48);

    ElastiqueDirect::usableSemitoneRange (ElastiqueDirect::Soloist, lo, hi);
    REQUIRE (lo == -17);
    REQUIRE (hi ==  41);
}

TEST_CASE ("cache requests nothing when no backend can render", "[cache]")
{
    // REAPER API も élastique も無い環境（＝素の VST ホストで DLL 未設定）。
    // ここで要求を積むと、背景スレッドが永久に空レンダリングを回し続ける。
    PitchCache pc;
    pc.setApi (nullptr);
    pc.setElastique (nullptr);

    SampleBuffer src;
    src.numChannels = 1;
    src.sampleRate  = 48000.0;
    src.numSamples  = 4800;
    src.data.assign (1, std::vector<float> (4800, 0.0f));

    REQUIRE (pc.configure (&src, 1, 0, 0, 48000.0, 0.0f, 1.0, 0.0f, 1.0f, 0));

    int lo = 0, hi = 0;
    pc.usableRange (lo, hi);
    REQUIRE (lo > hi);                 // 空範囲

    pc.request (0);
    pc.requestRange (-48, 48);
    REQUIRE_FALSE (pc.hasPending());
    REQUIRE (pc.pendingCount() == 0);
}

// NOTE: 「起動時にバックエンドが無く、あとから élastique DLL が設定された場合に
// 生成可能範囲を取り直す」ケースはここでは検証できない。ElastiqueDirect::isAvailable()
// は実 DLL を読めたかどうかで決まり、CI には elastique3.dll が無いため偽装できない。
// 実機では probe で確認済み:
//   DLL 未設定 → 範囲 1..0（空）・要求 0 件
//   同じ設定のまま setElastique() 後に configure → 範囲 -39..48・要求 88 件
// 修正前は後者も 1..0 / 0 件のままだった（問い合わせ条件にバックエンドの有無が
// 入っていなかったため）。PitchCache.cpp の probedReaperOk / probedElaOk を参照。
