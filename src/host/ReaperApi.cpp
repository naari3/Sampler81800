#include "ReaperApi.h"

#include <cstring>

// VST3 SDK（juce::juce_vst3_headers 経由で include パスが通る）
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>

namespace reaper
{
    using namespace Steinberg;   // uint32 / CStringA / TPtrInt / FUnknown / FUID をこの名前で解決

    #include "host/reaper_sdk/reaper_vst3_interfaces.h"

    // iid の実体はこの1TUで定義する（DESIGN.md §5.2 のデモ作法に準拠）。
    DEF_CLASS_IID (IReaperHostApplication)
    DEF_CLASS_IID (IReaperUIEmbedInterface)
}

namespace otomad::host
{

void ReaperApi::attachFromVST3 (void* p) noexcept
{
    hostApp = nullptr;
    if (p == nullptr)
        return;

    auto* unk = reinterpret_cast<Steinberg::FUnknown*> (p);
    void* obj = nullptr;
    if (unk->queryInterface (reaper::IReaperHostApplication::iid, &obj) == Steinberg::kResultOk && obj != nullptr)
        hostApp = obj;   // IReaperHostApplication*
}

void* ReaperApi::getFunction (const char* name) const noexcept
{
    if (hostApp == nullptr || name == nullptr)
        return nullptr;
    return static_cast<reaper::IReaperHostApplication*> (hostApp)->getReaperApi (name);
}

} // namespace otomad::host
