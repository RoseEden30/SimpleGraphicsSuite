#include "Compatibility.h"
#include "Config.h"
#include "DeviceHook.h"
#include "FrameBufferCache.h"
#include "Logging.h"
#include "NativeMenuIntegration.h"
#include "PresentHook.h"

#include "Features/Accessibility.h"
#include "Features/AntiAliasing.h"
#include "Features/DLSS.h"
#include "Features/PostProcessing.h"
#include "Features/Reflex.h"

namespace
{
    void OnPrePresent(REX::W32::ID3D11Device*, REX::W32::ID3D11DeviceContext*, REX::W32::IDXGISwapChain*)
    {
        UpdateSettingsSave();
    }

    void InstallSettingsSaveTick()
    {
        if (!PresentHook::Install()) {
            logger::warn("Couldn't install the Present hook - settings will be written on every change");
            return;
        }

        PresentHook::RegisterPrePresent(OnPrePresent);
        EnableSaveDebounce();
    }

    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad:
            // Before shaders start loading, matching Community Shaders' own
            // timing for the same BSShader::LoadShaders hook.
            if (!Compatibility::IsSuppressed(Compatibility::kAntiAliasing))
                AntiAliasing::InstallHooks();
            if (!Compatibility::IsSuppressed(Compatibility::kPostProcessing))
                PostProcessing::InstallHooks();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            // The D3D11 device and swapchain don't exist yet at
            // kPostPostLoad; Reflex needs them to detect the GPU and hook
            // Present. Every other plugin, NativeSystemMenuFramework included,
            // has also loaded by now.
            if (!Compatibility::IsSuppressed(Compatibility::kReflex))
                Reflex::InstallHooks();
            // Whether the card supports DLAA is fixed for the session, so it
            // is settled here rather than behind a button the player has to
            // find and press before the status rows say anything.
            if (!Compatibility::IsSuppressed(Compatibility::kAntiAliasing))
                DLSS::EnsureInitialized();
            Accessibility::InstallHooks();
            // Its only reader is DLSS, and support is fixed for the session.
            if (DLSS::IsSupported())
                FrameBufferCache::InstallHooks();
            // Before the menu registers, so its first change is debounced.
            InstallSettingsSaveTick();
            NativeMenuIntegration::Register();
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    Logging::Init();

    // Backing storage for every write_call/write_branch hook below.
    SKSE::AllocTrampoline(1 << 10);

    // Has to happen before the game creates its D3D11 device, so before
    // anything else here.
    DeviceHook::Install();

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    logger::info("{} {} loaded", plugin->GetName(), plugin->GetVersion());

    LoadSettings();
    Logging::SetVerbose(IsDebugEnabled());

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    return true;
}
