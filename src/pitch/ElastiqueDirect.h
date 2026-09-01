#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/SampleBuffer.h"

namespace otomad
{

//==============================================================================
/**
    élastique を DLL 直叩きで使うバックエンド（**実験的 / 再配布不可**）。

    REAPER 同梱の `elastique3.dll` を実行時ロードして、REAPER の外でも
    ピッチキャッシュのオフラインレンダに élastique を使えるようにする。

    公式 SDK が無いため、C export と vtable スロットを直接叩く：
      - `CreateInstance_E3(blockSize, channels, sampleRate, mode)` / `DestroyInstance_E3`
      - vtable[1] = ProcessData, vtable[5] = SetStretchQFactor, vtable[9] = Reset
      - ProcessData は起動プライミング中 rc=-1、出力が有効になると rc=0

    **重要**
    - élastique は zplane のプロプライエタリ技術で、REAPER にライセンスされたもの。
      DLL はリポジトリに含めず、ユーザーが自分の環境のパスを指定したときだけ動く。
    - オフラインレンダ専用。リアルタイム経路（Duration=Sync）では使わない
      （プライミング・FIFO・レイテンシ補償が必要になり、別物の複雑さになる）。
    - DLL が無い / ロード失敗なら `isAvailable()` は false のまま。呼び出し側は
      従来どおりフォールバックする（規約15: 無音を返さない）。
*/
class ElastiqueDirect
{
public:
    ElastiqueDirect() = default;
    ~ElastiqueDirect();

    ElastiqueDirect (const ElastiqueDirect&) = delete;
    ElastiqueDirect& operator= (const ElastiqueDirect&) = delete;

    /** DLL を読み込む。空文字なら既定の候補パスを順に試す。
        成功したら true。メッセージスレッドから呼ぶこと。 */
    bool load (const std::string& dllPath);
    void unload();

    bool isAvailable() const noexcept { return lib != nullptr; }
    const std::string& getLoadedPath() const noexcept { return loadedPath; }

    /** REAPER が入っていそうな既定の候補パス（先頭優先）。UI の初期値に使う。 */
    static std::vector<std::string> defaultCandidates();

    /** CreateInstance_E3 の第4引数。**実測で使えるのは 2 と 3 だけ。**

        mode 0/1 もインスタンス生成には成功するが、この 1:1 の ProcessData
        （入力 n サンプル → 出力 n サンプル）では 3 オクターブ下がった音になる。
        別の呼び出し規約（GetFramesNeeded による可変入力）が要ると思われるので使わない。

        - `Pro`     (2): 多声OK。-39〜+48 半音まで音程・レベルとも正確。
        - `Soloist` (3): 単声専用。和音だと片方の声部が消える。
                         下方向でレベルが pitch² 程度に落ちる（-24半音で入力の約4%）。

        注意: DLL の RTTI には Pro / Eff / Eff-mobile / SOLO の4系統があるが、
        **mode 2 がそのどれかは SDK が無いので断定できていない**。多声で動くことを
        実測したうえで UI 表記を "Elastique Pro" としているだけで、内部的に本当に
        CElastiqueProV3 かは未確認。
    */
    enum Mode { Pro = 2, Soloist = 3 };

    /** そのモードで音程・レベルが信用できる半音範囲（実測）。範囲外は空を返す。 */
    // **両モードとも ±24 半音まで。** DLL のピッチ factor は [0.25, 4.0] しか受け付けず、
    // それを外れると ProcessData が 0 を返さない＝出力が完全に空になる（音が変わるのではなく
    // 何も出ない）。以前ここは Pro -39..+48 / Soloist -17..+41 としていたが、これは誤り。
    //
    // 実測（220Hz の倍音入り信号を -48..+48 半音でレンダし、期待周波数の近傍で自己相関）:
    //   -48..-25 : 空
    //   -24..+24 : 全て OK（最大誤差 17 cent、ピーク 0.555 で一定＝下方向でもレベルは落ちない）
    //   +25..+48 : 空
    // Pro / Soloist で結果は完全に同じだった。
    static void usableSemitoneRange (Mode, int& lo, int& hi) noexcept
    {
        lo = -24; hi = 24;
    }

    /** オフラインで [base, base+n) をピッチシフトして返す。
        pitch は周波数比（2^(semi/12)）、timeRatio は「入力長/出力長」（1.0 で長さ維持）。
        失敗したら空を返す。背景スレッドから呼ぶ。 */
    std::vector<std::vector<float>> renderOffline (const SampleBuffer& src,
                                                   std::int64_t base, std::int64_t n,
                                                   int numChannels, double sampleRate,
                                                   double pitch, double timeRatio,
                                                   Mode mode) const;

private:
    void* lib = nullptr;          // HMODULE（windows.h をヘッダに持ち込まない）
    void* createFn  = nullptr;
    void* destroyFn = nullptr;
    std::string loadedPath;

    // キャッシュ生成は複数の背景スレッドで並列に走るが、この DLL のスレッド安全性は
    // 公式に保証されていない（SDK が無く RE で使っている）。実験機能なので安全側に倒し、
    // レンダを直列化する。並列度が落ちるだけで結果は変わらない。
    mutable std::mutex renderLock;
};

} // namespace otomad
