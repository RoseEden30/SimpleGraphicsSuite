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

        float& RadiusScaleSetting()
        {
            auto* ini = RE::INISettingCollection::GetSingleton();
            return ini->GetSetting("fPoissonRadiusScale:Display")->data.f;
        }

        // Backed up once so disabling restores the player's own ini value,
        // not an assumed default.
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

            float ambientSum = 0.0f;
            for (auto& column : sky->directionalAmbientColors)
                for (auto& color : column)
                    ambientSum += Luma(color);
            const float ambient = ambientSum / 6.0f;

            auto& shaderState = RE::BSShaderManager::State::GetSingleton();
            auto* sunLight = skyrim_cast<RE::NiDirectionalLight*>(
                shaderState.shadowSceneNode[0]->GetRuntimeData().sunLight->light.get());
            if (!sunLight)
                return kSoftnessInterior;

            auto&       isData = RE::ImageSpaceManager::GetSingleton()->GetImageSpaceData();
            const float sunlightScale =
                REL::Module::IsVR() ? isData.baseData.cinematic.brightness : isData.baseData.hdr.sunlightScale;
            const float sunlight =
                Luma(sunLight->GetLightRuntimeData().diffuse) * sunLight->GetLightRuntimeData().fade * sunlightScale;

            float sunAngle = sky->sun->GetRoot()->local.translate.z / 200.0f;
            sunAngle = 1.0f - std::clamp(sunAngle, 0.0f, 1.0f);

            float softness = 1.0f + (ambient / std::max(sunlight, 1e-4f)) + sunAngle;
            softness = std::pow(softness, kSoftnessCurve);
            return std::clamp(softness, 1.0f, kSoftnessMax);
        }

        void Update()
        {
            auto& setting = RadiusScaleSetting();
            if (!g_backedUp) {
                g_original = setting;
                g_backedUp = true;
            }

            const auto  settings = ActiveSettings();
            const float target = settings->masterEnabled && settings->softShadows.enabled
                                      ? kRadiusScaleBase * ComputeSoftness()
                                      : g_original;

            if (setting != target)
                setting = target;
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
