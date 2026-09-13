#include "SoftShadows.h"

#include "Config.h"
#include "VTablePatch.h"

namespace SoftShadows
{
    namespace
    {
        float Luma(const RE::NiColor& a_color)
        {
            return 0.2126f * a_color.red + 0.7152f * a_color.green + 0.0722f * a_color.blue;
        }

        // Only the on/off switch is exposed as a setting.
        constexpr float kRadiusScaleBase = 1.0f;
        constexpr float kSoftnessCurve = 4.0f;
        constexpr float kSoftnessMax = 20.0f;
        constexpr float kSoftnessInterior = 8.0f;

        float* RadiusScaleSetting()
        {
            auto* ini = RE::INISettingCollection::GetSingleton();
            auto* setting = ini ? ini->GetSetting("fPoissonRadiusScale:Display") : nullptr;
            return setting ? &setting->data.f : nullptr;
        }

        bool  g_backedUp = false;
        float g_original = 1.0f;

        float ComputeSoftness()
        {
            auto* sky = RE::Sky::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!sky || !cell)
                return kSoftnessInterior;

            if (cell->IsInteriorCell() || !sky->currentWeather || !sky->mode.any(RE::Sky::Mode::kFull))
                return kSoftnessInterior;

            auto* shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
            auto* sunShadowLight = shadowSceneNode ? shadowSceneNode->GetRuntimeData().sunLight : nullptr;
            auto* sunLight = sunShadowLight ? skyrim_cast<RE::NiDirectionalLight*>(sunShadowLight->light.get())
                                            : nullptr;
            auto* isManager = RE::ImageSpaceManager::GetSingleton();
            auto* sunRoot = sky->sun ? sky->sun->GetRoot() : nullptr;
            if (!sunLight || !isManager || !sunRoot)
                return kSoftnessInterior;

            float ambientSum = 0.0f;
            for (auto& column : sky->directionalAmbientColors)
                for (auto& color : column)
                    ambientSum += Luma(color);
            const float ambient = ambientSum / 6.0f;

            auto&       isData = isManager->GetImageSpaceData();
            const float sunlightScale =
                REL::Module::IsVR() ? isData.baseData.cinematic.brightness : isData.baseData.hdr.sunlightScale;
            const float sunlight =
                Luma(sunLight->GetLightRuntimeData().diffuse) * sunLight->GetLightRuntimeData().fade * sunlightScale;

            float sunAngle = sunRoot->local.translate.z / 200.0f;
            sunAngle = 1.0f - std::clamp(sunAngle, 0.0f, 1.0f);

            float softness = 1.0f + (ambient / std::max(sunlight, 1e-4f)) + sunAngle;
            softness = std::pow(softness, kSoftnessCurve);
            return std::clamp(softness, 1.0f, kSoftnessMax);
        }

        void Update()
        {
            auto* setting = RadiusScaleSetting();
            if (!setting)
                return;

            if (!g_backedUp) {
                g_original = *setting;
                g_backedUp = true;
            }

            const auto  settings = ActiveSettings();
            const float target = settings->masterEnabled && settings->softShadows.enabled
                                      ? kRadiusScaleBase * ComputeSoftness()
                                      : g_original;

            if (*setting != target)
                *setting = target;
        }

        using Update_t = void (*)(RE::PlayerCharacter*, float);
        Update_t g_original_Update = nullptr;

        void thunk_Update(RE::PlayerCharacter* a_this, float a_delta)
        {
            g_original_Update(a_this, a_delta);
            Update();
        }
    }

    void InstallHooks()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::error("SoftShadows: no PlayerCharacter singleton, hook not installed");
            return;
        }

        void* original = nullptr;
        if (!VTablePatch::PatchSlot(player, 0xAD, reinterpret_cast<void*>(&thunk_Update), &original)) {
            logger::error("SoftShadows: couldn't hook PlayerCharacter::Update");
            return;
        }
        g_original_Update = reinterpret_cast<Update_t>(original);

        logger::info("SoftShadows hooks installed");
    }
}
