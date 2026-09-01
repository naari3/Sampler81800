#include "PitchFlattener.h"

#include "PitchDetect.h"
#include "SampleBuffer.h"
#include "SourceReader.h"
#include "pitch/EngineResources.h"
#include "pitch/PhaseVocoderEngine.h"

#include <algorithm>
#include <cmath>

namespace otomad
{

namespace
{
    // 解析窓をモノ化して DC を抜き、RMS を返す
    double fillMonoWindow (const std::vector<std::vector<float>>& in, int numCh,
                           std::int64_t numSamples, std::int64_t at, int W,
                           std::vector<float>& buf)
    {
        buf.assign ((std::size_t) W, 0.0f);
        const float inv = 1.0f / (float) numCh;

        double mean = 0.0;
        for (int i = 0; i < W; ++i)
        {
            const std::int64_t idx = at + i;
            if (idx < 0 || idx >= numSamples) continue;
            float m = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                m += in[(std::size_t) ch][(std::size_t) idx];
            buf[(std::size_t) i] = m * inv;
            mean += buf[(std::size_t) i];
        }
        mean /= (double) W;

        double energy = 0.0;
        for (int i = 0; i < W; ++i)
        {
            buf[(std::size_t) i] -= (float) mean;
            energy += (double) buf[(std::size_t) i] * buf[(std::size_t) i];
        }
        return std::sqrt (energy / (double) W);
    }

    FlattenResult analyseImpl (const std::vector<std::vector<float>>& in, int numCh,
                               std::int64_t numSamples, double sampleRate,
                               std::int64_t rangeStart, std::int64_t rangeEnd,
                               const FlattenOptions& opt)
    {
        FlattenResult r;
        if (numCh <= 0 || numSamples <= 0 || sampleRate <= 0.0 || (int) in.size() < numCh)
            return r;

        rangeStart = std::clamp<std::int64_t> (rangeStart, 0, numSamples);
        rangeEnd   = std::clamp<std::int64_t> (rangeEnd,   0, numSamples);
        if (rangeEnd - rangeStart < 1024)
            return r;

        const int W   = std::max (256, (int) std::llround (opt.frameSeconds * sampleRate));
        const int hop = std::max (32,  (int) std::llround (opt.hopSeconds   * sampleRate));

        // 窓の「中心」が区間内に入るフレームを並べる。窓自体は区間外へはみ出してよい
        // （境界で窓を縮めると検出が荒れるので、実データをそのまま読む）。
        r.contour.hopSeconds   = (double) hop / sampleRate;
        r.contour.startSeconds = (double) rangeStart / sampleRate;

        std::vector<float> buf;
        std::vector<float> valid;   // 信頼できたフレームの MIDI 値（中央値用）

        for (std::int64_t c = rangeStart; c < rangeEnd; c += hop)
        {
            const std::int64_t at = c - W / 2;
            float note = 0.0f;

            const double rms = fillMonoWindow (in, numCh, numSamples, at, W, buf);
            if (rms >= opt.minRms)
            {
                double freq = 0.0;
                const double aper = pitchdetect::yinWindow (buf.data(), W, sampleRate, freq);
                if (aper < opt.maxAperiodicity && freq > 0.0)
                {
                    note = (float) pitchdetect::hzToMidi (freq);
                    valid.push_back (note);
                }
            }
            r.contour.midi.push_back (note);
        }

        r.voicedFrames = (int) valid.size();
        if (valid.size() < 3)       // 明確な音程なし
            return r;

        std::sort (valid.begin(), valid.end());
        r.detectedMidi = (double) valid[valid.size() / 2];
        r.targetNote   = (int) std::lround (r.detectedMidi);
        r.ok = true;
        return r;
    }

    // 補正カーブ（半音）をフレーム単位で作る。
    // 無効フレームは直前の値を保持し、先頭側は最初の有効値で埋める（0 に落とさない）。
    std::vector<float> buildCorrection (const PitchContour& contour, double target,
                                        float strength, double smoothSeconds)
    {
        const std::size_t n = contour.midi.size();
        std::vector<float> corr (n, 0.0f);
        if (n == 0) return corr;

        float held = 0.0f;
        bool  seen = false;
        std::size_t firstValid = n;

        for (std::size_t i = 0; i < n; ++i)
        {
            if (contour.midi[i] > 0.0f)
            {
                held = (float) (target - (double) contour.midi[i]);
                if (! seen) { seen = true; firstValid = i; }
            }
            corr[i] = held;   // 無声区間は直前の補正量を保持（0に落とすと子音でワブる）
        }
        if (! seen) return corr;
        for (std::size_t i = 0; i < firstValid; ++i)   // 先頭の無声部
            corr[i] = corr[firstValid];

        // 一次平滑。位相ズレを出さないよう前向き→後ろ向きの往復で掛ける。
        if (smoothSeconds > 0.0 && contour.hopSeconds > 0.0)
        {
            const double tau = smoothSeconds / contour.hopSeconds;
            if (tau > 0.5)
            {
                const float a = (float) std::exp (-1.0 / tau);
                float y = corr[0];
                for (std::size_t i = 0; i < n; ++i) { y = a * y + (1.0f - a) * corr[i]; corr[i] = y; }
                y = corr[n - 1];
                for (std::size_t i = n; i-- > 0; )   { y = a * y + (1.0f - a) * corr[i]; corr[i] = y; }
            }
        }

        for (auto& v : corr) v *= strength;
        return corr;
    }
}

//==============================================================================
FlattenResult analyseOnly (const std::vector<std::vector<float>>& in, int numChannels,
                           std::int64_t numSamples, double sampleRate,
                           std::int64_t rangeStart, std::int64_t rangeEnd,
                           const FlattenOptions& opt)
{
    return analyseImpl (in, numChannels, numSamples, sampleRate, rangeStart, rangeEnd, opt);
}

FlattenResult flattenToSinglePitch (const std::vector<std::vector<float>>& in, int numChannels,
                                    std::int64_t numSamples, double sampleRate,
                                    std::int64_t rangeStart, std::int64_t rangeEnd,
                                    const FlattenOptions& opt)
{
    auto r = analyseImpl (in, numChannels, numSamples, sampleRate, rangeStart, rangeEnd, opt);
    if (! r.ok)
        return r;

    const auto corr = buildCorrection (r.contour, (double) r.targetNote,
                                       std::clamp (opt.strength, 0.0f, 1.0f), opt.smoothSeconds);
    if (corr.empty())
    { r.ok = false; return r; }

    // フレーム単位の補正[半音] → サンプル単位の pitchRatio。
    // **半音ドメインで線形補間してから 2^(x/12) する**（規約4: Hz直線補間は禁止）。
    const double hop = std::max (1.0, opt.hopSeconds * sampleRate);
    std::vector<float> ratio ((std::size_t) numSamples, 1.0f);
    for (std::int64_t i = 0; i < numSamples; ++i)
    {
        const double f = ((double) (i - rangeStart)) / hop;   // フレーム座標
        double semi;
        if (f <= 0.0)                             semi = corr.front();
        else if (f >= (double) (corr.size() - 1)) semi = corr.back();
        else
        {
            const std::size_t k = (std::size_t) f;
            const double t = f - (double) k;
            semi = (1.0 - t) * (double) corr[k] + t * (double) corr[k + 1];
        }
        ratio[(std::size_t) i] = (float) std::exp2 (semi / 12.0);
    }

    // 既存の長さ保持エンジンへ流す。timeRatio=1.0 なので長さは変わらない
    // （規約5: pitchRatio と timeRatio を癒着させない）。
    SampleBuffer src;
    src.data        = in;
    src.numChannels = numChannels;
    src.numSamples  = numSamples;
    src.sampleRate  = sampleRate;

    SourceReader reader;
    reader.configure (&src, 0, numSamples, false);

    EngineResources res;
    res.prepare (sampleRate);

    PhaseVocoderEngine eng;
    eng.setPhaseLock (true);   // 位相ロックを切るとフェーズィな響きになる
    PitchEngineContext ctx { sampleRate, opt.blockSize, numChannels };
    eng.prepare (ctx, res);
    eng.reset();

    // エンジンは intrinsicLatency ぶん遅れて出るので、その分だけ余分に回して先頭を捨てる。
    const std::int64_t hopComp = res.pvHop;   // 補正カーブを前倒しする量（下の rp で使う）
    const std::int64_t lat     = (std::int64_t) eng.getIntrinsicLatency();
    const std::int64_t total = numSamples + lat;

    std::vector<std::vector<float>> out ((std::size_t) numChannels,
                                         std::vector<float> ((std::size_t) total, 0.0f));
    std::vector<float*> ptrs ((std::size_t) numChannels);
    std::vector<float>  ratioBlk ((std::size_t) opt.blockSize, 1.0f);

    double srcPos = 0.0;
    for (std::int64_t pos = 0; pos < total; pos += opt.blockSize)
    {
        const int nn = (int) std::min<std::int64_t> (opt.blockSize, total - pos);
        for (int ch = 0; ch < numChannels; ++ch)
            ptrs[(std::size_t) ch] = out[(std::size_t) ch].data() + pos;

        // ratio は「入力のどこを読んでいるか」に対応させる。ただし srcPos そのままでは
        // **合成ホップ1つぶん先の補正**を渡してしまう。エンジンはブロック先頭の
        // pitchRatio でフレームを1つ合成し（規約10）、そのフレームの音は srcPos より
        // 1ホップ手前の入力から作られるため。
        //
        // 実測（正解の補正カーブを直接与えて残差を測定, 48kHz）:
        //   offset   0 → 22.5 cent 残る
        //   offset -512 → 10.2 cent（最小）
        //   offset -1024→ 11.3 cent
        // blockSize を 128/512/1024 と変えても最小は -512 のままだったので、
        // ズレは blockSize ではなく hop に紐づく。WSOLA でも同じ傾向（-512 が最小）。
        const std::int64_t rp = (std::int64_t) srcPos - hopComp + opt.ratioOffsetSamples;
        for (int i = 0; i < nn; ++i)
            ratioBlk[(std::size_t) i] =
                ratio[(std::size_t) std::clamp<std::int64_t> (rp + i, 0, numSamples - 1)];

        eng.process (reader, srcPos, ptrs.data(), numChannels, nn, ratioBlk.data(), 1.0);
    }

    r.audio.assign ((std::size_t) numChannels, {});
    for (int ch = 0; ch < numChannels; ++ch)
        r.audio[(std::size_t) ch].assign (out[(std::size_t) ch].begin() + lat,
                                          out[(std::size_t) ch].begin() + lat + numSamples);

    // 出来上がった音の実際の音程を測り直す。strength<1 では targetNote まで寄らないので、
    // 呼び出し側が Root/Cent を決めるにはこの実測値が要る（モデルで推定すると必ずズレる）。
    const auto post = analyseImpl (r.audio, numChannels, numSamples, sampleRate,
                                   rangeStart, rangeEnd, opt);
    r.resultMidi = post.ok ? post.detectedMidi : (double) r.targetNote;
    return r;
}

} // namespace otomad
