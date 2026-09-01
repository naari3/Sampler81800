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

TEST_CASE ("elastique usable range is +-24 semitones for both modes", "[cache]")
{
    // 実測値。ここを広げると「空のレンダリング」を延々と要求することになるので、
    // 広げるなら必ず実機で測り直すこと（probe: 220Hz 倍音入りを -48..+48 でレンダ）。
    for (auto mode : { ElastiqueDirect::Pro, ElastiqueDirect::Soloist })
    {
        int lo = 0, hi = 0;
        ElastiqueDirect::usableSemitoneRange (mode, lo, hi);
        REQUIRE (lo == -24);
        REQUIRE (hi ==  24);
    }
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
