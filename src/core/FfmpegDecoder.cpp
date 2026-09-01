#include "FfmpegDecoder.h"

#include <cstdlib>

#include "Utf8.h"

namespace otomad
{

namespace
{
    constexpr int kVersionTimeoutMs = 5000;
    constexpr int kDecodeTimeoutMs  = 120000;   // 長尺でも落ちないよう余裕を持たせる

   #if JUCE_WINDOWS
    const char* kExeName = "ffmpeg.exe";
    const char* kPathSep = ";";
   #else
    const char* kExeName = "ffmpeg";
    const char* kPathSep = ":";
   #endif

    // 一時ファイルはまとめて置いて確実に消せるようにする
    juce::File tempDir()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("OtoMadSampler");
    }
}

//==============================================================================
juce::File FfmpegDecoder::find()
{
    juce::Array<juce::File> candidates;

    // 1) PATH（一番ふつうの入れ方。winget / choco / scoop はどれもここを通る）
    if (auto* p = std::getenv ("PATH"))
    {
        juce::StringArray dirs;
        dirs.addTokens (juce::String::fromUTF8 (p), kPathSep, "");
        for (auto d : dirs)
        {
            d = d.trim().unquoted();   // PATH の要素は引用符付きのことがある
            if (d.isNotEmpty())
                candidates.add (juce::File::createFileWithoutCheckingPath (d).getChildFile (kExeName));
        }
    }

   #if JUCE_WINDOWS
    // 2) PATH を通していないケースの保険
    const auto local = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getParentDirectory().getChildFile ("Local");
    candidates.add (local.getChildFile ("Microsoft/WinGet/Links/ffmpeg.exe"));
    candidates.add (juce::File ("C:/ProgramData/chocolatey/bin/ffmpeg.exe"));
    candidates.add (juce::File ("C:/ffmpeg/bin/ffmpeg.exe"));
   #endif

    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;

    return {};
}

bool FfmpegDecoder::verify (const juce::File& ffmpeg)
{
    if (! ffmpeg.existsAsFile())
        return false;

    juce::ChildProcess proc;
    if (! proc.start (juce::StringArray { ffmpeg.getFullPathName(), "-version" },
                      juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return false;

    const auto out = proc.readAllProcessOutput();
    if (! proc.waitForProcessToFinish (kVersionTimeoutMs))
    {
        proc.kill();
        return false;
    }
    return proc.getExitCode() == 0 && out.containsIgnoreCase ("ffmpeg version");
}

//==============================================================================
juce::File FfmpegDecoder::decodeToWav (const juce::File& ffmpeg,
                                       const void* data, std::size_t size,
                                       const juce::String& originalName,
                                       juce::String& errorOut)
{
    errorOut.clear();

    if (! ffmpeg.existsAsFile() || data == nullptr || size == 0)
    { errorOut = u8 ("ffmpeg が設定されていません"); return {}; }

    auto dir = tempDir();
    if (! dir.createDirectory())
    { errorOut = u8 ("一時ディレクトリを作成できません: ") + dir.getFullPathName(); return {}; }

    // 入力の拡張子はデマルチプレクサのヒント。ffmpeg は中身も見るので厳密でなくてよい。
    auto ext = juce::File::createLegalFileName (originalName).fromLastOccurrenceOf (".", true, false);
    if (! ext.startsWithChar ('.') || ext.length() > 8)
        ext = ".bin";

    const auto stamp = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    const auto in  = dir.getChildFile ("in_"  + stamp + ext);
    const auto out = dir.getChildFile ("out_" + stamp + ".wav");

    if (! in.replaceWithData (data, size))
    { errorOut = u8 ("一時ファイルに書き出せません: ") + in.getFullPathName(); return {}; }

    // -nostdin: 端末の無い環境で stdin 待ちハングを防ぐ（これが無いと固まることがある）
    // -vn     : 映像を落とす。既定のストリーム選択が最良の音声トラックを拾う
    // pcm_f32le: 中間なので劣化・クリップを避けるため float のまま出す
    // サンプルレート/チャンネル数は変換しない。ホストSRへの変換は SampleLoader が行う（二重変換禁止・規約16）
    const juce::StringArray args {
        ffmpeg.getFullPathName(),
        "-nostdin", "-v", "error", "-y",
        "-i", in.getFullPathName(),
        "-vn", "-c:a", "pcm_f32le", "-f", "wav",
        out.getFullPathName()
    };

    juce::ChildProcess proc;
    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        errorOut = u8 ("ffmpeg を起動できません: ") + ffmpeg.getFullPathName();
        in.deleteFile();
        return {};
    }

    const auto log = proc.readAllProcessOutput();   // パイプが閉じる＝プロセス終了まで待つ
    if (! proc.waitForProcessToFinish (kDecodeTimeoutMs))
        proc.kill();

    in.deleteFile();

    if (proc.getExitCode() != 0 || ! out.existsAsFile() || out.getSize() == 0)
    {
        errorOut = log.trim().isNotEmpty() ? log.trim() : u8 ("ffmpeg のデコードに失敗しました");
        out.deleteFile();
        return {};
    }
    return out;
}

} // namespace otomad
