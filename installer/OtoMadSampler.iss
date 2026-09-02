; OtoMadSampler Windows インストーラー (Inno Setup 6)
;
; ビルド:
;   ISCC.exe /DAppVersion=0.5.0 /DVst3Dir=<OtoMadSampler.vst3 の親フォルダ> installer\OtoMadSampler.iss
;
; 署名について:
;   ここでは署名しない。証明書のある環境でだけ CI が signtool を掛ける
;   （.iss に SignTool を書くと、証明書の無い環境でビルドが通らなくなるため）。
;   .vst3 は DAW が読み込むだけなので SmartScreen の対象にならないが、
;   この .exe は対象になる。未署名のうちは README の回避手順を案内すること。

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Vst3Dir
  #define Vst3Dir "..\build\OtoMadSampler_artefacts\RelWithDebInfo\VST3"
#endif

#define AppName    "OtoMadSampler"
#define Publisher  "neon-uriel"
#define AppUrl     "https://github.com/neon-uriel/Sampler81800"

[Setup]
AppId={{7C2F1A96-5E4B-4C3D-9E21-0B7A6D4F8C15}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases

; VST3 は共通フォルダ(Program Files)へ入れるので管理者権限が要る
PrivilegesRequired=admin
; 置き先は固定。ユーザーに選ばせると DAW が見つけられない場所に入る
DisableDirPage=yes
DisableProgramGroupPage=yes

; **{app} を共通 VST3 フォルダにしてはいけない。**
; Inno はアンインストーラ(unins000.exe)を {app} 直下に置くので、そこを
; {commoncf64}\VST3 にすると他社プラグインが並ぶ共有フォルダに実行ファイルを撒くことになり、
; 同じ方式の別インストーラとも unins001, unins002 … と衝突する。
; {app} は自分専用の場所にして、.vst3 だけを [Files] の DestDir で共通フォルダへ入れる。
DefaultDirName={autopf}\{#Publisher}\{#AppName}
Uninstallable=yes
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={commoncf64}\VST3\OtoMadSampler.vst3\Contents\x86_64-win\OtoMadSampler.vst3

OutputDir=..\dist
OutputBaseFilename=OtoMadSampler-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";  MessagesFile: "compiler:Default.isl"

[Files]
; .vst3 はファイルではなくフォルダ。中身ごと再帰的に入れる。
; ArchitecturesInstallIn64BitMode があるので {commoncf} でも 64bit 側を指すが、
; 取り違えると DAW から見えなくなるだけで気づきにくいので明示する。
Source: "{#Vst3Dir}\OtoMadSampler.vst3\*"; DestDir: "{commoncf64}\VST3\OtoMadSampler.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; {app} の外に入れたので、空になったフォルダは明示的に消す
Type: filesandordirs; Name: "{commoncf64}\VST3\OtoMadSampler.vst3"
; 発行元フォルダは他製品が入っている可能性があるので、空のときだけ消す
Type: dirifempty; Name: "{autopf}\{#Publisher}"

[Code]
// DAW がプラグインを掴んでいると、置き換えが「アクセスが拒否されました」で失敗する。
// **そのとき Inno はロールバックして、既に入っていたファイルまで消してしまう**
// （実際に Contents\Resources が消えて、前のインストールが壊れた）。
// なのでファイルに触る前に検出して、何もせずに止める。
//
// 「使用中か」は非破壊には調べにくい。ロードされた DLL でも名前変更は通るので、
// 書き込み用に共有無しで開けるかどうかで判定する。開けなければ誰かが掴んでいる。

// FILE_ATTRIBUTE_NORMAL は Inno が既に定義しているので再定義しない
const
  GENERIC_WRITE        = $40000000;
  OPEN_EXISTING        = 3;
  INVALID_HANDLE_VALUE = -1;

function CreateFileW(lpFileName: string; dwDesiredAccess, dwShareMode: Cardinal;
  lpSecurityAttributes: Integer; dwCreationDisposition, dwFlagsAndAttributes: Cardinal;
  hTemplateFile: Integer): Integer;
  external 'CreateFileW@kernel32.dll stdcall';

function CloseHandle(hObject: Integer): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function PluginIsInUse(): Boolean;
var
  Path: string;
  H: Integer;
begin
  Path := ExpandConstant('{commoncf64}\VST3\OtoMadSampler.vst3\Contents\x86_64-win\OtoMadSampler.vst3');
  if not FileExists(Path) then
  begin
    Result := False;   // 新規インストール
    exit;
  end;
  // dwShareMode=0 ＝ 共有を許さずに開く。他が掴んでいれば失敗する。
  H := CreateFileW(Path, GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  Result := (H = INVALID_HANDLE_VALUE);
  if not Result then
    CloseHandle(H);
end;

// ファイルを1つも触っていない段階で呼ばれる。空でない文字列を返すと中止。
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if PluginIsInUse() then
    Result := CustomMessage('PluginInUse')
  else
    Result := '';
end;

[CustomMessages]
japanese.PluginInUse=OtoMadSampler が使用中です。%n%nDAW（REAPER / Ableton Live など）が起動していると、プラグインのファイルを置き換えられません。%n%nDAW をすべて終了してから、もう一度インストーラーを実行してください。
english.PluginInUse=OtoMadSampler is currently in use.%n%nThe plug-in files cannot be replaced while a DAW (REAPER, Ableton Live, ...) is running.%n%nPlease close all DAWs and run this installer again.

[Messages]
japanese.WelcomeLabel2=このウィザードは [name/ver] をインストールします。%n%nDAW を起動したままだとファイルの上書きに失敗します。先に終了してください。%n%nインストール後、DAW を再起動するか、プラグインの再スキャンを実行してください。
english.WelcomeLabel2=This will install [name/ver] on your computer.%n%nClose your DAW first - files cannot be replaced while it is running.%n%nAfter installing, restart your DAW or rescan plug-ins.
