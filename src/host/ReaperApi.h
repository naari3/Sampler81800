#pragma once

namespace otomad::host
{

//==============================================================================
/**
    REAPER 拡張API取得の唯一の窓口。DESIGN.md §5.2 / 規約2。
    **VST3/REAPER SDK 型をヘッダに漏らさない**（hostApp は不透明 void*）。
    非REAPERホストでは isAvailable()==false のまま安全に振る舞う（スキャン時クラッシュ防止）。
*/
class ReaperApi
{
public:
    // VST3 の IHostApplication (FUnknown*) を受け取り、IReaperHostApplication を queryInterface する。
    // 失敗しても例外を投げない。メッセージスレッドから呼ぶ。
    void attachFromVST3 (void* hostApplicationFUnknown) noexcept;

    bool isAvailable() const noexcept { return hostApp != nullptr; }

    // REAPER拡張API関数を名前で取得（null安全）。REAPER上でも版により null になりうる。
    void* getFunction (const char* name) const noexcept;

private:
    void* hostApp = nullptr;   // 不透明: reaper::IReaperHostApplication*
};

} // namespace otomad::host
