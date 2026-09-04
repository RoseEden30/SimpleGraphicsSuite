#include "NativeMenuIntegration.h"

#include "Compatibility.h"
#include "Config.h"
#include "Features/AntiAliasing.h"
#include "Features/Debug.h"
#include "Features/DLSS.h"
#include "Features/LUT.h"
#include "Features/PostProcessing.h"
#include "Features/Reflex.h"
#include "NativeSystemMenuFramework.h"

#include <cmath>
#include <cstdio>

namespace NativeMenuIntegration
{
    namespace
    {
        // Shared by every row - the setters have already applied the value.
        // Requested, not written: a reset commits one row at a time and those
        // belong in a single write.
        void __stdcall OnSettingCommit(float) { RequestSaveSettings(); }

        // Vanilla sliders already report/expect a 0.0-1.0 fraction (the
        // ScrollBar's own 0-20 position is divided/multiplied internally
        // before it ever reaches us), matching our own float ranges exactly.
        float __stdcall GetVignette() { return ActiveSettings()->postProcessing.vignette; }
        void  __stdcall SetVignette(float a_value)
        {
            EditableSettings().postProcessing.vignette = a_value;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetFilmGrain() { return ActiveSettings()->postProcessing.filmGrain; }
        void  __stdcall SetFilmGrain(float a_value)
        {
            EditableSettings().postProcessing.filmGrain = a_value;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetLensFlare() { return ActiveSettings()->postProcessing.lensFlare; }
        void  __stdcall SetLensFlare(float a_value)
        {
            EditableSettings().postProcessing.lensFlare = a_value;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetSharpening() { return ActiveSettings()->postProcessing.sharpening; }
        void  __stdcall SetSharpening(float a_value)
        {
            EditableSettings().postProcessing.sharpening = a_value;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetMotionBlurStrength() { return ActiveSettings()->postProcessing.motionBlurStrength; }
        void  __stdcall SetMotionBlurStrength(float a_value)
        {
            EditableSettings().postProcessing.motionBlurStrength = a_value;
            PublishSettings();
            RequestSaveSettings();
        }
        float __stdcall GetVignetteSneakOnly() { return ActiveSettings()->postProcessing.vignetteSneakOnly ? 1.0f : 0.0f; }
        void  __stdcall SetVignetteSneakOnly(float a_value)
        {
            EditableSettings().postProcessing.vignetteSneakOnly = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }
        // With no vignette to show, sneak-only has nothing to gate.
        bool __stdcall IsVignetteSneakOnlyEnabled() { return ActiveSettings()->postProcessing.vignette > 0.0f; }

        // Index 0 is "Off" (maps to antiAliasing.enabled), so the method
        // itself sits one index higher here than in antiAliasing.method/kMethods.
        float __stdcall GetAntiAliasingMethod()
        {
            const auto& aa = ActiveSettings()->antiAliasing;
            return aa.enabled ? static_cast<float>(aa.method) + 1.0f : 0.0f;
        }
        void __stdcall SetAntiAliasingMethod(float a_value)
        {
            const auto index = static_cast<std::uint32_t>(a_value);
            auto&      aa = EditableSettings().antiAliasing;
            aa.enabled = index != 0;
            if (aa.enabled)
                aa.method = index - 1;
            PublishSettings();
            RequestSaveSettings();
        }


        constexpr float kMaxDeblur = 3.0f;

        float __stdcall GetTextureDeblur() { return -ActiveSettings()->antiAliasing.mipLodBias / kMaxDeblur; }
        void  __stdcall SetTextureDeblur(float a_value)
        {
            EditableSettings().antiAliasing.mipLodBias = -(a_value * kMaxDeblur);
            PublishSettings();
            RequestSaveSettings();
        }
        bool __stdcall IsTextureDeblurEnabled()
        {
            const auto& aa = ActiveSettings()->antiAliasing;
            return aa.enabled && aa.method == 0;
        }

        float __stdcall GetSoftShadowsEnabled() { return ActiveSettings()->softShadows.enabled ? 1.0f : 0.0f; }
        void  __stdcall SetSoftShadowsEnabled(float a_value)
        {
            EditableSettings().softShadows.enabled = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        // Arrows, not a ScrollBar - a slider has exactly 21 stops, too coarse
        // for 5-degree steps over this range.
        constexpr float kFovSteps[] = { 60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 115, 120 };

        std::size_t FovIndexFor(float a_degrees)
        {
            std::size_t closest = 0;
            for (std::size_t i = 1; i < std::size(kFovSteps); ++i) {
                if (std::abs(a_degrees - kFovSteps[i]) < std::abs(a_degrees - kFovSteps[closest]))
                    closest = i;
            }
            return closest;
        }

        std::vector<std::string> FovOptions()
        {
            std::vector<std::string> options;
            for (float step : kFovSteps)
                options.push_back(std::to_string(static_cast<int>(step)) + "\xC2\xB0");
            return options;
        }

        // The live camera value until the player picks their own, so the
        // arrows start wherever their FOV already is.
        float __stdcall GetFieldOfViewDegrees()
        {
            const auto& fov = ActiveSettings()->fieldOfView;
            float       degrees = fov.degrees;
            if (!fov.customized) {
                if (auto* camera = RE::PlayerCamera::GetSingleton())
                    degrees = camera->GetRuntimeData2().worldFOV;
            }
            return static_cast<float>(FovIndexFor(degrees));
        }
        void __stdcall SetFieldOfViewDegrees(float a_value)
        {
            const auto index =
                std::clamp(static_cast<std::size_t>(a_value), std::size_t{ 0 }, std::size(kFovSteps) - 1);
            auto& fov = EditableSettings().fieldOfView;
            fov.customized = true;
            fov.degrees = kFovSteps[index];
            PublishSettings();
            RequestSaveSettings();
        }

        // Off, then AMD's own FSR1 quality presets - matches the values
        // Upscaling.cpp's own render scale slider is documented against.
        constexpr float kUpscalePresets[] = { 1.0f, 0.77f, 0.67f, 0.58f, 0.50f };

        float __stdcall GetUpscalingPreset()
        {
            const auto& upscaling = ActiveSettings()->upscaling;
            if (!upscaling.enabled)
                return 0.0f;

            std::size_t closest = 1;
            for (std::size_t i = 1; i < std::size(kUpscalePresets); ++i) {
                if (std::abs(upscaling.renderScale - kUpscalePresets[i]) <
                    std::abs(upscaling.renderScale - kUpscalePresets[closest]))
                    closest = i;
            }
            return static_cast<float>(closest);
        }

        void __stdcall SetUpscalingPreset(float a_value)
        {
            const auto index = std::clamp(static_cast<std::size_t>(a_value), std::size_t{ 0 },
                std::size(kUpscalePresets) - 1);
            auto& upscaling = EditableSettings().upscaling;
            upscaling.enabled = index != 0;
            if (index != 0)
                upscaling.renderScale = kUpscalePresets[index];
            PublishSettings();
            RequestSaveSettings();
        }

        // FXAA and DLAA both ignore the render scale - see Upscaling::IsActive.
        bool __stdcall IsRenderResolutionEnabled()
        {
            const auto& aa = ActiveSettings()->antiAliasing;
            return !(aa.enabled && (aa.method == 1 || aa.method == 2));
        }

        float __stdcall GetColorblindMode() { return static_cast<float>(ActiveSettings()->accessibility.colorblindMode); }
        void  __stdcall SetColorblindMode(float a_value)
        {
            EditableSettings().accessibility.colorblindMode = static_cast<std::uint32_t>(a_value);
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetColorblindStrength() { return ActiveSettings()->accessibility.colorblindStrength; }
        void  __stdcall SetColorblindStrength(float a_value)
        {
            EditableSettings().accessibility.colorblindStrength = a_value;
            PublishSettings();
            RequestSaveSettings();
        }
        // The whole pass is skipped in Off mode.
        bool __stdcall IsColorblindStrengthEnabled() { return ActiveSettings()->accessibility.colorblindMode != 0; }

        float __stdcall GetHighContrastStrength() { return ActiveSettings()->accessibility.highContrastStrength; }
        void  __stdcall SetHighContrastStrength(float a_value)
        {
            EditableSettings().accessibility.highContrastStrength = a_value;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetReflexEnabled() { return ActiveSettings()->reflex.enabled ? 1.0f : 0.0f; }
        void  __stdcall SetReflexEnabled(float a_value)
        {
            EditableSettings().reflex.enabled = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }
        // Every other Reflex row is gated on the module being active.
        bool __stdcall IsReflexEnabled() { return ActiveSettings()->reflex.enabled; }

        float __stdcall GetReflexLowLatency() { return ActiveSettings()->reflex.lowLatencyMode ? 1.0f : 0.0f; }
        void  __stdcall SetReflexLowLatency(float a_value)
        {
            EditableSettings().reflex.lowLatencyMode = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetReflexBoost() { return ActiveSettings()->reflex.lowLatencyBoost ? 1.0f : 0.0f; }
        void  __stdcall SetReflexBoost(float a_value)
        {
            EditableSettings().reflex.lowLatencyBoost = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetReflexMarkers() { return ActiveSettings()->reflex.useMarkersToOptimize ? 1.0f : 0.0f; }
        void  __stdcall SetReflexMarkers(float a_value)
        {
            EditableSettings().reflex.useMarkersToOptimize = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetReflexFpsLimitEnabled() { return ActiveSettings()->reflex.useFpsLimit ? 1.0f : 0.0f; }
        void  __stdcall SetReflexFpsLimitEnabled(float a_value)
        {
            EditableSettings().reflex.useFpsLimit = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        // The native ScrollBar has exactly 21 stops, so there is one step per
        // stop: a plain 30-240 range would land on values like 40.5, while
        // these stay round and still cover the usual refresh rates.
        // formatValue prints the real figure next to the label.
        constexpr float kFpsLimitPresets[] = { 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 75.0f, 80.0f, 90.0f, 100.0f,
            110.0f, 120.0f, 130.0f, 140.0f, 144.0f, 150.0f, 160.0f, 165.0f, 180.0f, 200.0f, 220.0f, 240.0f };
        constexpr std::size_t kFpsLimitLast = std::size(kFpsLimitPresets) - 1;
        static_assert(std::size(kFpsLimitPresets) == 21, "one preset per ScrollBar stop");

        std::size_t FpsLimitIndex(float a_slider)
        {
            const auto scaled = a_slider * static_cast<float>(kFpsLimitLast);
            return std::clamp(static_cast<std::size_t>(scaled + 0.5f), std::size_t{ 0 }, kFpsLimitLast);
        }

        float __stdcall GetReflexFpsLimit()
        {
            const auto  fps = ActiveSettings()->reflex.fpsLimit;
            std::size_t closest = 0;
            for (std::size_t i = 1; i <= kFpsLimitLast; ++i) {
                if (std::abs(fps - kFpsLimitPresets[i]) < std::abs(fps - kFpsLimitPresets[closest]))
                    closest = i;
            }
            return static_cast<float>(closest) / static_cast<float>(kFpsLimitLast);
        }
        void __stdcall SetReflexFpsLimit(float a_value)
        {
            EditableSettings().reflex.fpsLimit = kFpsLimitPresets[FpsLimitIndex(a_value)];
            PublishSettings();
            RequestSaveSettings();
        }
        // Just the number - the label already says FPS, and a longer string
        // makes the row's fixed-width text field shrink its font.
        void __stdcall FormatFpsLimit(float a_value, char* a_buffer, int a_bufferSize)
        {
            std::snprintf(a_buffer, a_bufferSize, "%d", static_cast<int>(kFpsLimitPresets[FpsLimitIndex(a_value)]));
        }

        bool __stdcall IsFpsLimitValueEnabled()
        {
            const auto& reflex = ActiveSettings()->reflex;
            return reflex.enabled && reflex.useFpsLimit;
        }

        float __stdcall GetMasterEnabled() { return ActiveSettings()->masterEnabled ? 1.0f : 0.0f; }
        void  __stdcall SetMasterEnabled(float a_value)
        {
            EditableSettings().masterEnabled = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetDebugEnabled() { return IsDebugEnabled() ? 1.0f : 0.0f; }
        void  __stdcall SetDebugEnabled(float a_value) { ::SetDebugEnabled(a_value != 0.0f); }

        // Vanilla's own reset (T) already covers the tab on screen, row by
        // row through defaultValue. This is the wider one: every module at
        // once, including tabs the player never opened.
        void __stdcall OnResetToDefaults()
        {
            EditableSettings() = Settings{};
            PublishSettings();
            RequestSaveSettings();
        }

        // Read-only status rows, shown alongside the dev actions below. One
        // line each - a native row has a single fixed-width text field.
        void __stdcall GetInfoHooks(char* a_buffer, int a_bufferSize)
        {
            const auto ok = NativeSystemMenuFramework::Translate("$SGS_OK");
            const auto failed = NativeSystemMenuFramework::Translate("$SGS_FAILED");
            std::snprintf(a_buffer, a_bufferSize, "%s: AA %s, Post %s", NativeSystemMenuFramework::Translate("$SGS_INFO_HOOKS").c_str(),
                (AntiAliasing::IsInstalled() ? ok : failed).c_str(),
                (PostProcessing::IsInstalled() ? ok : failed).c_str());
        }

        constexpr const char* kAAMethodNames[] = { "TAA", "FXAA", "DLAA" };

        void __stdcall GetInfoAntiAliasing(char* a_buffer, int a_bufferSize)
        {
            const auto& aa = ActiveSettings()->antiAliasing;
            const bool  active = ActiveSettings()->masterEnabled && aa.enabled;
            std::snprintf(a_buffer, a_bufferSize, "%s: %s (%s)", NativeSystemMenuFramework::Translate("$SGS_ANTI_ALIASING").c_str(),
                kAAMethodNames[std::min<std::uint32_t>(aa.method, 2)],
                NativeSystemMenuFramework::Translate(active ? "$SGS_ACTIVE" : "$SGS_OFF").c_str());
        }

        void __stdcall GetInfoDlss(char* a_buffer, int a_bufferSize)
        {
            // The full reason can run long and would shrink the row's font
            // to nothing; it is already in the log.
            std::snprintf(a_buffer, a_bufferSize, "%s: %s", NativeSystemMenuFramework::Translate("$SGS_DLAA").c_str(),
                NativeSystemMenuFramework::Translate(DLSS::IsSupported() ? "$SGS_SUPPORTED" : "$SGS_UNSUPPORTED_SEE_LOG").c_str());
        }

        void __stdcall GetInfoPostProcessing(char* a_buffer, int a_bufferSize)
        {
            std::snprintf(a_buffer, a_bufferSize, "%s: %zu", NativeSystemMenuFramework::Translate("$SGS_INFO_SHADERS").c_str(),
                PostProcessing::ReplacedShaderCount());
        }

        void __stdcall GetInfoGpu(char* a_buffer, int a_bufferSize)
        {
            std::snprintf(a_buffer, a_bufferSize, "%s: %s", NativeSystemMenuFramework::Translate("$SGS_INFO_GPU").c_str(),
                Debug::GpuSummary().c_str());
        }

        void __stdcall GetInfoResolution(char* a_buffer, int a_bufferSize)
        {
            const auto& state = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();
            const auto  screen = RE::BSGraphics::Renderer::GetScreenSize();
            std::snprintf(a_buffer, a_bufferSize, "%s: %ux%u at %.0f%%",
                NativeSystemMenuFramework::Translate("$SGS_INFO_SCREEN").c_str(), screen.width, screen.height,
                state.dynamicResolutionWidthRatio * 100.0f);
        }

        void __stdcall GetInfoReflex(char* a_buffer, int a_bufferSize)
        {
            const auto& reflex = ActiveSettings()->reflex;
            const bool  active = ActiveSettings()->masterEnabled && reflex.enabled;
            const auto label = NativeSystemMenuFramework::Translate("$SGS_REFLEX");
            if (!Reflex::IsSupported())
                std::snprintf(a_buffer, a_bufferSize, "%s: %s", label.c_str(),
                    NativeSystemMenuFramework::Translate("$SGS_UNSUPPORTED").c_str());
            else if (!active)
                std::snprintf(a_buffer, a_bufferSize, "%s: %s", label.c_str(),
                    NativeSystemMenuFramework::Translate("$SGS_OFF").c_str());
            else if (reflex.useFpsLimit)
                std::snprintf(a_buffer, a_bufferSize, "%s: %s %.0f", label.c_str(),
                    NativeSystemMenuFramework::Translate("$SGS_ON_FPS_LIMIT").c_str(), reflex.fpsLimit);
            else
                std::snprintf(a_buffer, a_bufferSize, "%s: %s", label.c_str(),
                    NativeSystemMenuFramework::Translate("$SGS_ON_NO_FPS_LIMIT").c_str());
        }

        void __stdcall OnReloadShaders() { PostProcessing::ReloadShadersFromDisk(); }

        void __stdcall GetCompatibilityLabel(char* a_buffer, int a_bufferSize)
        {
            std::snprintf(a_buffer, a_bufferSize, "%s: %s",
                NativeSystemMenuFramework::Translate("$SGS_HANDLED_BY").c_str(),
                std::string(Compatibility::DetectedNames()).c_str());
        }

        void __stdcall GetVersionLabel(char* a_buffer, int a_bufferSize)
        {
            const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
            const auto  name = std::string(plugin->GetName());
            std::snprintf(a_buffer, a_bufferSize, "%s %s", name.c_str(), plugin->GetVersion().string().c_str());
        }

        float __stdcall GetPostProcessingEnabled() { return ActiveSettings()->postProcessing.enabled ? 1.0f : 0.0f; }
        void  __stdcall SetPostProcessingEnabled(float a_value)
        {
            EditableSettings().postProcessing.enabled = a_value != 0.0f;
            PublishSettings();
            RequestSaveSettings();
        }
        bool __stdcall IsGradingEnabled() { return ActiveSettings()->postProcessing.enabled; }

        float __stdcall GetTonemapMethod() { return static_cast<float>(ActiveSettings()->postProcessing.tonemapMethod) - 1.0f; }
        void  __stdcall SetTonemapMethod(float a_value)
        {
            EditableSettings().postProcessing.tonemapMethod = static_cast<std::uint32_t>(a_value) + 1;
            PublishSettings();
            RequestSaveSettings();
        }

        void __stdcall FormatDecimal2(float a_value, char* a_buffer, int a_bufferSize)
        {
            std::snprintf(a_buffer, a_bufferSize, "%.2f", static_cast<double>(a_value));
        }

        float __stdcall GetExposure() { return (ActiveSettings()->postProcessing.exposure + 2.0f) / 4.0f; }
        void  __stdcall SetExposure(float a_value)
        {
            EditableSettings().postProcessing.exposure = a_value * 4.0f - 2.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetContrast() { return (ActiveSettings()->postProcessing.contrast - 0.5f) / 1.5f; }
        void  __stdcall SetContrast(float a_value)
        {
            EditableSettings().postProcessing.contrast = 0.5f + a_value * 1.5f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetSaturation() { return ActiveSettings()->postProcessing.saturation / 2.0f; }
        void  __stdcall SetSaturation(float a_value)
        {
            EditableSettings().postProcessing.saturation = a_value * 2.0f;
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetBloomIntensity() { return ActiveSettings()->postProcessing.bloomIntensity; }
        void  __stdcall SetBloomIntensity(float a_value)
        {
            EditableSettings().postProcessing.bloomIntensity = a_value;
            PublishSettings();
            RequestSaveSettings();
        }

        // Captured once at Register() - a static option list, same as every
        // other dropdown, so picking up files dropped in after startup needs
        // a restart.
        std::vector<std::string> g_lutOptions;

        float __stdcall GetLutIndex()
        {
            const auto& name = ActiveSettings()->postProcessing.lutName;
            for (std::size_t i = 1; i < g_lutOptions.size(); ++i)
                if (g_lutOptions[i] == name)
                    return static_cast<float>(i);
            return 0.0f;
        }
        void __stdcall SetLutIndex(float a_value)
        {
            const auto index = static_cast<std::size_t>(a_value);
            EditableSettings().postProcessing.lutName = (index == 0 || index >= g_lutOptions.size())
                                                              ? std::string()
                                                              : g_lutOptions[index];
            PublishSettings();
            RequestSaveSettings();
        }

        float __stdcall GetLutStrength() { return ActiveSettings()->postProcessing.lutStrength; }
        void  __stdcall SetLutStrength(float a_value)
        {
            EditableSettings().postProcessing.lutStrength = a_value;
            PublishSettings();
            RequestSaveSettings();
        }
        bool __stdcall IsLutStrengthEnabled() { return !ActiveSettings()->postProcessing.lutName.empty(); }
    }

    void Register()
    {
        if (!NativeSystemMenuFramework::IsInstalled())
            return;

        // Keeps this mod's tabs together and ordered against other mods'.
        NativeSystemMenuFramework::SetModName("SimpleGraphicsSuite");

        NativeSystemMenuFramework::SetVanillaTabDescription("$SGS_ACCESSIBILITY_TAB", "$SGS_ACCESSIBILITY_TAB_DESC");
        NativeSystemMenuFramework::SetVanillaTabDescription("$SGS_PERFORMANCE_TAB", "$SGS_PERFORMANCE_TAB_DESC");
        NativeSystemMenuFramework::SetVanillaTabDescription("$SGS_EFFECTS_TAB", "$SGS_EFFECTS_TAB_DESC");
        NativeSystemMenuFramework::SetVanillaTabDescription("$SGS_LUT_TAB", "$SGS_LUT_TAB_DESC");
        NativeSystemMenuFramework::SetVanillaTabDescription(
            "$SGS_SIMPLE_GRAPHICS_SUITE_TAB", "$SGS_SIMPLE_GRAPHICS_SUITE_TAB_DESC");

        using NativeSystemMenuFramework::AddVanillaSetting;
        using Type = NativeSystemMenuFramework::SettingType;

        const bool postProcessing = !Compatibility::IsSuppressed(Compatibility::kPostProcessing);
        const bool antiAliasing = !Compatibility::IsSuppressed(Compatibility::kAntiAliasing);
        const bool reflex = !Compatibility::IsSuppressed(Compatibility::kReflex);
        const bool softShadows = !Compatibility::IsSuppressed(Compatibility::kSoftShadows);
        const bool fieldOfView = !Compatibility::IsSuppressed(Compatibility::kFieldOfView);

        if (postProcessing) {
            AddVanillaSetting("Display", Type::kSlider, "$SGS_MOTION_BLUR", &GetMotionBlurStrength,
                &SetMotionBlurStrength, 0.0f, {}, nullptr, nullptr, "$SGS_MOTION_BLUR_DESC", &OnSettingCommit);
            AddVanillaSetting("Display", Type::kSlider, "$SGS_VIGNETTE", &GetVignette, &SetVignette, 0.0f, {}, nullptr,
                nullptr, "$SGS_VIGNETTE_DESC", &OnSettingCommit);
            AddVanillaSetting("Display", Type::kCheckbox, "$SGS_VIGNETTE_SNEAK_ONLY", &GetVignetteSneakOnly,
                &SetVignetteSneakOnly, 0.0f, {}, &IsVignetteSneakOnlyEnabled, nullptr,
                "$SGS_VIGNETTE_SNEAK_ONLY_DESC", &OnSettingCommit);
            AddVanillaSetting("Display", Type::kSlider, "$SGS_SHARPENING", &GetSharpening, &SetSharpening, 0.0f, {},
                nullptr, nullptr, "$SGS_SHARPENING_DESC", &OnSettingCommit);
        }

        if (antiAliasing) {
            AddVanillaSetting("Display", Type::kDropdown, "$SGS_ANTI_ALIASING", &GetAntiAliasingMethod,
                &SetAntiAliasingMethod, 1.0f, { "$SGS_OFF", "$SGS_TAA", "$SGS_FXAA", "$SGS_DLAA" }, nullptr, nullptr,
                "$SGS_ANTI_ALIASING_DESC", &OnSettingCommit);
            AddVanillaSetting("Display", Type::kSlider, "$SGS_TEXTURE_DEBLUR", &GetTextureDeblur, &SetTextureDeblur,
                0.9f / kMaxDeblur, {}, &IsTextureDeblurEnabled, &FormatDecimal2,
                "$SGS_TEXTURE_DEBLUR_DESC", &OnSettingCommit);
        }

        if (softShadows) {
            AddVanillaSetting("Display", Type::kCheckbox, "$SGS_SOFT_SHADOWS", &GetSoftShadowsEnabled,
                &SetSoftShadowsEnabled, 0.0f, {}, nullptr, nullptr, "$SGS_SOFT_SHADOWS_DESC", &OnSettingCommit);
        }

        if (fieldOfView) {
            AddVanillaSetting("Display", Type::kDropdown, "$SGS_FIELD_OF_VIEW", &GetFieldOfViewDegrees,
                &SetFieldOfViewDegrees, 3.0f, FovOptions(), nullptr, nullptr, "$SGS_FIELD_OF_VIEW_DESC",
                &OnSettingCommit);
        }

        if (postProcessing) {
            AddVanillaSetting("Display", Type::kDropdown, "$SGS_RENDER_RESOLUTION", &GetUpscalingPreset,
                &SetUpscalingPreset, 0.0f,
                { "$SGS_NATIVE_OFF", "$SGS_77_ULTRA_QUALITY", "$SGS_67_QUALITY", "$SGS_58_BALANCED", "$SGS_50_PERFORMANCE" },
                &IsRenderResolutionEnabled, nullptr,
                "$SGS_RENDER_RESOLUTION_DESC", &OnSettingCommit);
        }

        AddVanillaSetting("$SGS_ACCESSIBILITY_TAB", Type::kDropdown, "$SGS_COLORBLIND_MODE", &GetColorblindMode,
            &SetColorblindMode, 0.0f,
            { "$SGS_OFF", "$SGS_PROTANOPIA", "$SGS_DEUTERANOPIA", "$SGS_TRITANOPIA", "$SGS_GRAYSCALE" },
            nullptr, nullptr, "$SGS_COLORBLIND_MODE_DESC", &OnSettingCommit);
        AddVanillaSetting("$SGS_ACCESSIBILITY_TAB", Type::kSlider, "$SGS_COLORBLIND_STRENGTH", &GetColorblindStrength,
            &SetColorblindStrength, 1.0f, {}, &IsColorblindStrengthEnabled, nullptr,
            "$SGS_COLORBLIND_STRENGTH_DESC", &OnSettingCommit);
        AddVanillaSetting("$SGS_ACCESSIBILITY_TAB", Type::kSlider, "$SGS_HIGH_CONTRAST", &GetHighContrastStrength,
            &SetHighContrastStrength, 0.0f, {}, nullptr, nullptr, "$SGS_HIGH_CONTRAST_DESC", &OnSettingCommit);

        if (reflex) {
            AddVanillaSetting("$SGS_PERFORMANCE_TAB", Type::kCheckbox, "$SGS_ENABLED", &GetReflexEnabled, &SetReflexEnabled, 1.0f, {},
                nullptr, nullptr, "$SGS_REFLEX_ENABLED_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_PERFORMANCE_TAB", Type::kCheckbox, "$SGS_LOW_LATENCY_MODE", &GetReflexLowLatency,
                &SetReflexLowLatency, 1.0f, {}, &IsReflexEnabled, nullptr,
                "$SGS_LOW_LATENCY_MODE_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_PERFORMANCE_TAB", Type::kCheckbox, "$SGS_LOW_LATENCY_BOOST", &GetReflexBoost, &SetReflexBoost,
                0.0f, {}, &IsReflexEnabled, nullptr,
                "$SGS_LOW_LATENCY_BOOST_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_PERFORMANCE_TAB", Type::kCheckbox, "$SGS_USE_MARKERS_TO_OPTIMIZE", &GetReflexMarkers,
                &SetReflexMarkers, 0.0f, {}, &IsReflexEnabled, nullptr,
                "$SGS_USE_MARKERS_TO_OPTIMIZE_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_PERFORMANCE_TAB", Type::kCheckbox, "$SGS_ENABLE_FPS_LIMIT", &GetReflexFpsLimitEnabled,
                &SetReflexFpsLimitEnabled, 0.0f, {}, &IsReflexEnabled, nullptr,
                "$SGS_ENABLE_FPS_LIMIT_DESC", &OnSettingCommit);
            // Default is 60 fps, which sits at index 3 of the preset table.
            AddVanillaSetting("$SGS_PERFORMANCE_TAB", Type::kSlider, "$SGS_FPS_LIMIT", &GetReflexFpsLimit, &SetReflexFpsLimit,
                3.0f / static_cast<float>(kFpsLimitLast), {}, &IsFpsLimitValueEnabled, &FormatFpsLimit,
                "$SGS_FPS_LIMIT_DESC", &OnSettingCommit);
        }

        if (postProcessing) {
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kCheckbox, "$SGS_ENABLED", &GetPostProcessingEnabled,
                &SetPostProcessingEnabled, 1.0f, {}, nullptr, nullptr,
                "$SGS_EFFECTS_ENABLED_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kDropdown, "$SGS_TONEMAP_CURVE", &GetTonemapMethod, &SetTonemapMethod,
                3.0f, { "$SGS_CHANNEL", "$SGS_PEAK", "$SGS_AVERAGE_LUMA", "$SGS_FROSTBYTE", "$SGS_ACES" }, &IsGradingEnabled, nullptr,
                "$SGS_TONEMAP_CURVE_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kSlider, "$SGS_EXPOSURE", &GetExposure, &SetExposure, 0.5f, {},
                &IsGradingEnabled, &FormatDecimal2, "$SGS_EXPOSURE_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kSlider, "$SGS_CONTRAST", &GetContrast, &SetContrast,
                (1.39f - 0.5f) / 1.5f, {}, &IsGradingEnabled, &FormatDecimal2,
                "$SGS_CONTRAST_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kSlider, "$SGS_SATURATION", &GetSaturation, &SetSaturation, 0.5f, {},
                &IsGradingEnabled, &FormatDecimal2, "$SGS_SATURATION_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kSlider, "$SGS_BLOOM_INTENSITY", &GetBloomIntensity, &SetBloomIntensity,
                0.5f, {}, &IsGradingEnabled, &FormatDecimal2, "$SGS_BLOOM_INTENSITY_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kSlider, "$SGS_FILM_GRAIN", &GetFilmGrain, &SetFilmGrain,
                0.0f, {}, nullptr, nullptr, "$SGS_FILM_GRAIN_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_EFFECTS_TAB", Type::kSlider, "$SGS_LENS_FLARE", &GetLensFlare, &SetLensFlare,
                0.0f, {}, nullptr, nullptr, "$SGS_LENS_FLARE_DESC", &OnSettingCommit);

            // Same static-option-list constraint as every other dropdown -
            // picking up files dropped in after this scan needs a restart.
            LUT::Rescan();
            g_lutOptions.assign(1, "None");
            for (const auto& name : LUT::AvailableNames())
                g_lutOptions.push_back(name);
            AddVanillaSetting("$SGS_LUT_TAB", Type::kDropdown, "$SGS_COLOR_GRADING_LUT", &GetLutIndex, &SetLutIndex, 0.0f,
                g_lutOptions, nullptr, nullptr,
                "$SGS_COLOR_GRADING_LUT_DESC", &OnSettingCommit);
            AddVanillaSetting("$SGS_LUT_TAB", Type::kSlider, "$SGS_LUT_STRENGTH", &GetLutStrength, &SetLutStrength, 1.0f, {},
                &IsLutStrengthEnabled, nullptr, "$SGS_LUT_STRENGTH_DESC", &OnSettingCommit);
        }

        // Suite-wide switches, kept apart from the per-module tabs.
        NativeSystemMenuFramework::AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetVersionLabel);
        if (!Compatibility::DetectedNames().empty())
            NativeSystemMenuFramework::AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetCompatibilityLabel);
        AddVanillaSetting("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", Type::kCheckbox, "$SGS_ENABLED", &GetMasterEnabled, &SetMasterEnabled,
            1.0f, {}, nullptr, nullptr, "$SGS_MASTER_ENABLED_DESC", &OnSettingCommit);
        AddVanillaSetting("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", Type::kCheckbox, "$SGS_DEBUG_MODE", &GetDebugEnabled, &SetDebugEnabled,
            0.0f, {}, nullptr, nullptr, "$SGS_DEBUG_MODE_DESC");
        NativeSystemMenuFramework::AddVanillaButton("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", "$SGS_RESET_ALL_TO_DEFAULTS", &OnResetToDefaults);

        // Status rows and dev actions, gated behind [Debug] Enabled=1.
        if (IsDebugEnabled()) {
            using NativeSystemMenuFramework::AddVanillaLabel;
            AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoGpu);
            // One row for both modules, so it only says anything while both run.
            if (antiAliasing && postProcessing)
                AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoHooks);
            if (antiAliasing) {
                AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoAntiAliasing);
                AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoDlss);
            }
            if (postProcessing)
                AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoPostProcessing);
            if (reflex)
                AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoReflex);
            AddVanillaLabel("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", &GetInfoResolution);

            if (postProcessing)
                NativeSystemMenuFramework::AddVanillaButton("$SGS_SIMPLE_GRAPHICS_SUITE_TAB", "$SGS_RELOAD_SHADERS_FROM_DISK",
                    &OnReloadShaders);
        }

        logger::info("NativeMenuIntegration: registered vanilla settings");
    }
}
