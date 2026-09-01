#pragma once

#include <juce_core/juce_core.h>

namespace otomad
{

//==============================================================================
/**
    UTF-8 のナロー文字列リテラルから juce::String を作る。

    **非ASCIIを含むリテラルを juce::String に渡すときは必ずこれを通すこと。**

    `juce::String (const char*)` は `CharPointer_ASCII` を使う実装で、
    バイト値をそのまま code point にする（＝Latin-1 解釈）。ソースは /utf-8 で
    コンパイルしているのでリテラルは UTF-8 バイト列であり、直接渡すと文字化けする。
      "直" (E7 9B B4) → "ç›´"

    デバッグビルドでは JUCE 側が jassert で気づかせてくれるが、
    RelWithDebInfo（＝配布ビルド）では assert が消えるので**黙って化ける**。
    実際 v0.4.1 まで、SHIFTER 欄の状態表示や ffmpeg のエラーメッセージが化けていた。
*/
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

/*  外部から受け取った文字列も同じ罠にかかる（REAPER のモード名、ffmpeg の出力、パス等）。

    - `std::string` は **そのまま渡せば UTF-8 として扱われる**
      （`String(const std::string&)` → `createFromFixedLength` → `CharPointer_UTF8`）。
    - **`.c_str()` を付けると `const char*` オーバーロードに落ちて ASCII 扱いになる。**
      v0.4.1 まで REAPER のモード名がこれで化けていた（"élastique" → "Ã©lastique"）。
    - `const char*` しか無いときは `juce::String::fromUTF8()` か上の `u8()` を使う。

    リテラルの走査だけでは見つからない種類のバグなので、`.c_str()` を書いたら必ず疑うこと。
*/

} // namespace otomad
