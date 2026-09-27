#include "AntiAliasing.h"

#include "Config.h"
#include "DLSS.h"
#include "RE/BSGraphics.h"
#include "Upscaling.h"

namespace AntiAliasing
{
    namespace
    {
        // Only the linear filter sampler gets the bias.
        RE::BSGraphics::SamplerStates g_backup{};
        REX::W32::ID3D11SamplerState* g_modified[RE::BSGraphics::SamplerStates::kAddressModes]{};

        bool  g_backedUp = false;
        bool  g_biasApplied = false;
        float g_appliedBias = 0.0f;
        // The game's own TAA/FXAA state, restored when the suite is disabled.
        bool g_controllingNativeAA = false;
        bool g_originalTaaEnabled = false;
        bool g_originalFxaaActive = false;

        void ReleaseModified()
        {
            for (auto& state : g_modified) {
                if (state) {
                    state->Release();
                    state = nullptr;
                }
            }
        }

        // Swaps in linear samplers with a_bias, or restores the game's.
        void Apply(bool a_enabled, float a_bias)
        {
            constexpr auto kLinear = RE::BSGraphics::SamplerStates::kLinearFilter;

            auto* live = RE::BSGraphics::SamplerStates::GetSingleton();

            if (!g_backedUp) {
                for (std::size_t i = 0; i < RE::BSGraphics::SamplerStates::kAddressModes; ++i)
                    for (std::size_t k = 0; k < RE::BSGraphics::SamplerStates::kFilterModes; ++k)
                        g_backup.states[i][k] = live->states[i][k];

                g_backedUp = true;
            }

            if (!a_enabled) {
                for (std::size_t i = 0; i < RE::BSGraphics::SamplerStates::kAddressModes; ++i)
                    live->states[i][kLinear] = g_backup.states[i][kLinear];
                ReleaseModified();
                g_biasApplied = false;
                g_appliedBias = 0.0f;
                return;
            }

            ReleaseModified();

            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            for (std::size_t i = 0; i < RE::BSGraphics::SamplerStates::kAddressModes; ++i) {
                auto* original = g_backup.states[i][kLinear];
                if (!original)
                    continue;

                REX::W32::D3D11_SAMPLER_DESC desc{};
                original->GetDesc(&desc);
                desc.mipLODBias = a_bias;

                device->CreateSamplerState(&desc, &g_modified[i]);
            }

            for (std::size_t i = 0; i < RE::BSGraphics::SamplerStates::kAddressModes; ++i)
                live->states[i][kLinear] = g_modified[i] ? g_modified[i] : g_backup.states[i][kLinear];

            g_biasApplied = true;
            g_appliedBias = a_bias;
        }

        void Update()
        {
            const auto settings = ActiveSettings();
            const auto& config = settings->antiAliasing;

            auto* taaState = RE::BSGraphics::TAAState::GetSingleton();
            auto* isManager = RE::ImageSpaceManager::GetSingleton();
            const auto fxaaIndex = RE::ImageSpaceManager::GetCurrentIndex(RE::ImageSpaceManager::ISFXAA);
            RE::ImageSpaceEffect* fxaa =
                (isManager && fxaaIndex < isManager->effects.size()) ? isManager->effects[fxaaIndex] : nullptr;

            const bool activeModule = settings->masterEnabled && config.enabled;

            if (settings->masterEnabled) {
                if (!g_controllingNativeAA) {
                    g_originalTaaEnabled = taaState && taaState->IsTAAEnabled();
                    g_originalFxaaActive = fxaa && fxaa->active;
                    g_controllingNativeAA = true;
                }
                // The flag gates the whole temporal mode (jitter, motion vectors), so DLAA needs it too.
                if (taaState && taaState->inner)
                    taaState->inner->taaEnabled = config.enabled && (config.method == 0 || config.method == 2);
                if (fxaa)
                    fxaa->active = config.enabled && config.method == 1;
            } else if (g_controllingNativeAA) {
                if (taaState && taaState->inner)
                    taaState->inner->taaEnabled = g_originalTaaEnabled;
                if (fxaa)
                    fxaa->active = g_originalFxaaActive;
                g_controllingNativeAA = false;
            }

            const bool  taaEnabled = taaState && taaState->IsTAAEnabled();
            const bool  dlssActive = activeModule && config.method == 2 && DLSS::IsSupported();
            const float bias = DLSS::RecommendedMipBias();

            if (dlssActive != g_biasApplied || (dlssActive && bias != g_appliedBias)) {
                if (dlssActive != g_biasApplied)
                    logger::info("Anti-aliasing: master={} enabled={} taa={} -> DLAA mip bias={}",
                        settings->masterEnabled, config.enabled, taaEnabled, dlssActive);
                Apply(dlssActive, bias);
            }
        }

        struct Main_UpdateViewport
        {
            static void thunk(RE::BSGraphics::State* a_state)
            {
                func(a_state);
                Update();
                Upscaling::Update(*a_state);

                const auto settings = ActiveSettings();
                const auto& config = settings->antiAliasing;
                if (settings->masterEnabled && config.enabled && config.method == 2)
                    DLSS::UpdateJitter(a_state);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // The engine overwrites the jitter here, after Main_UpdateViewport.
        struct Main_UpdateJitter
        {
            static void thunk(RE::BSGraphics::State* a_state)
            {
                func(a_state);

                const auto settings = ActiveSettings();
                const auto& config = settings->antiAliasing;
                if (settings->masterEnabled && config.enabled && config.method == 2)
                    DLSS::ApplyProjectionJitter(a_state);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void InstallHooks()
    {
        const auto target =
            RELOCATION_ID(35556, 36555).address() + REL::VariantOffset(0x2D, 0x2D, 0x25).offset();

        Main_UpdateViewport::func = SKSE::GetTrampoline().write_call<5>(target, Main_UpdateViewport::thunk);

        // GOG is at 0x133, Steam since 1.7.99 too (0xE2 before). Checked against open-shaders.
        const bool             isGOG = !GetModuleHandleW(L"steam_api64.dll");
        const std::uintptr_t   steamOffset = REL::Module::IsAtLeast(REL::Version(1, 7, 99, 0)) ? 0x133 : 0xE2;
        const auto             jitterTarget = RELOCATION_ID(75460, 77245).address() +
            REL::Relocate<std::uintptr_t>(0xE5, isGOG ? 0x133 : steamOffset, 0x104);
        Main_UpdateJitter::func = SKSE::GetTrampoline().write_call<5>(jitterTarget, Main_UpdateJitter::thunk);

        logger::info("Anti-aliasing hook installed");
    }

    bool IsInstalled() { return Main_UpdateViewport::func.address() != 0; }
}
