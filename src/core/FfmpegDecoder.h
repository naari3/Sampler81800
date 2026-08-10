#pragma once

#include <juce_core/juce_core.h>

namespace otomad
{

//==============================================================================
/**
    外部 ffmpeg を使ったデコード（**同梱しない / ユーザー指定**）。

    JUCE の `AudioFormatManager` が読めなかったファイルだけをここへ回す。
    そうすることで拡張子の一覧を持たずに済み、mp4 / m4a / webm / mkv / mov /
    opus など「ffmpeg が読めるものは全部」対応できる。ffmpeg 未設定なら
    従来どおりの挙動のまま（規約15: 黙って壊れた結果を返さない）。

    ffmpeg 本体はライセンス構成が配布形態によって変わる（GPL/LGPL、非フリーの
    コーデックを有効にしたビルドもある）ため、**バイナリはリポジトリに含めず
    同梱もしない**。ユーザーが自分の環境の実行ファイルを指定する。
    Audacity 等と同じ方針。
*/
class FfmpegDecoder
{
public:
    /** PATH と既定の候補場所から ffmpeg.exe を探す。見つからなければ無効な File。 */
    static juce::File find();

    /** `-version` を実行して実際に動くか確かめる（別物の同名ファイル対策）。 */
    static bool verify (const juce::File& ffmpeg);

    /** バイト列を一時 wav へデコードする。

        成功したら出力 wav の File を返す（**呼び出し側が削除すること**）。
        失敗したら無効な File を返し、`errorOut` に ffmpeg の stderr を入れる。
        背景スレッドから呼ぶこと（プロセス起動と待ち合わせでブロックする）。

        @param originalName 拡張子をデマルチプレクサのヒントとして使う（推測もするので必須ではない）
    */
    static juce::File decodeToWav (const juce::File& ffmpeg,
                                   const void* data, std::size_t size,
                                   const juce::String& originalName,
                                   juce::String& errorOut);
};

} // namespace otomad
