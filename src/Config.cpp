#include "Config.h"

#include <SimpleIni.h>
#include <atomic>
#include <chrono>

namespace
{
    Settings g_editing;

    // Never null, so a hook running before the first publish reads defaults
    // instead of dereferencing nothing.
    std::atomic<SettingsPtr> g_active{ std::make_shared<const Settings>() };

    // Ini-only, read once at startup - deliberately not part of Settings, so
    // it's untouched by Reset to defaults and never written back by the menu.
    bool g_debugEnabled = false;
    bool g_ignoreModConflicts = false;

    std::vector<void (*)()> g_publishCallbacks;

    // Long enough to swallow a burst, short enough that quitting right after
    // a change keeps it.
    constexpr auto kSaveQuietPeriod = std::chrono::milliseconds(400);

    bool g_debounceSaves = false;

    // Requested on the menu's thread, completed on the frame loop's.
    std::atomic<bool>                                  g_savePending{ false };
    std::atomic<std::chrono::steady_clock::time_point> g_lastSaveRequest{};

    std::filesystem::path GetIniPath()
    {
        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        return std::filesystem::path(std::format("Data/SKSE/Plugins/{}.ini", plugin->GetName()));
    }

    void ReadGeneral(const CSimpleIniA& ini, Settings& settings)
    {
        settings.masterEnabled = ini.GetBoolValue("General", "Enabled", settings.masterEnabled);
    }

    void WriteGeneral(CSimpleIniA& ini, const Settings& settings)
    {
        ini.SetBoolValue("General", "Enabled", settings.masterEnabled);
    }

    void ReadAntiAliasing(const CSimpleIniA& ini, Settings::AntiAliasing& section)
    {
        section.method = static_cast<std::uint32_t>(ini.GetLongValue("AntiAliasing", "Method", section.method));
        section.enabled = ini.GetBoolValue("AntiAliasing", "Enabled", section.enabled);
        section.mipLodBias = static_cast<float>(ini.GetDoubleValue("AntiAliasing", "MipLODBias", section.mipLodBias));
    }

    void WriteAntiAliasing(CSimpleIniA& ini, const Settings::AntiAliasing& section)
    {
        ini.SetLongValue("AntiAliasing", "Method", static_cast<long>(section.method));
        ini.SetBoolValue("AntiAliasing", "Enabled", section.enabled);
        ini.SetDoubleValue("AntiAliasing", "MipLODBias", section.mipLodBias);
    }

    void ReadPostProcessing(const CSimpleIniA& ini, Settings::PostProcessing& section)
    {
        section.enabled = ini.GetBoolValue("PostProcessing", "Enabled", section.enabled);
        section.sharpening = static_cast<float>(ini.GetDoubleValue("PostProcessing", "Sharpening", section.sharpening));
        section.exposure = static_cast<float>(ini.GetDoubleValue("PostProcessing", "Exposure", section.exposure));
        section.contrast = static_cast<float>(ini.GetDoubleValue("PostProcessing", "Contrast", section.contrast));
        section.saturation = static_cast<float>(ini.GetDoubleValue("PostProcessing", "Saturation", section.saturation));
        section.bloomIntensity =
            static_cast<float>(ini.GetDoubleValue("PostProcessing", "BloomIntensity", section.bloomIntensity));
        section.tonemapMethod =
            static_cast<std::uint32_t>(ini.GetLongValue("PostProcessing", "TonemapMethod", section.tonemapMethod));
        section.motionBlurStrength =
            static_cast<float>(ini.GetDoubleValue("PostProcessing", "MotionBlurStrength", section.motionBlurStrength));
        section.vignette = static_cast<float>(ini.GetDoubleValue("PostProcessing", "Vignette", section.vignette));
        section.vignetteSneakOnly =
            ini.GetBoolValue("PostProcessing", "VignetteSneakOnly", section.vignetteSneakOnly);
        section.filmGrain = static_cast<float>(ini.GetDoubleValue("PostProcessing", "FilmGrain", section.filmGrain));
        section.lutName = ini.GetValue("PostProcessing", "LUT", section.lutName.c_str());
        section.lutStrength =
            static_cast<float>(ini.GetDoubleValue("PostProcessing", "LUTStrength", section.lutStrength));
    }

    void WritePostProcessing(CSimpleIniA& ini, const Settings::PostProcessing& section)
    {
        ini.SetBoolValue("PostProcessing", "Enabled", section.enabled);
        ini.SetDoubleValue("PostProcessing", "Sharpening", section.sharpening);
        ini.SetDoubleValue("PostProcessing", "Exposure", section.exposure);
        ini.SetDoubleValue("PostProcessing", "Contrast", section.contrast);
        ini.SetDoubleValue("PostProcessing", "Saturation", section.saturation);
        ini.SetDoubleValue("PostProcessing", "BloomIntensity", section.bloomIntensity);
        ini.SetLongValue("PostProcessing", "TonemapMethod", static_cast<long>(section.tonemapMethod));
        ini.SetDoubleValue("PostProcessing", "MotionBlurStrength", section.motionBlurStrength);
        ini.SetDoubleValue("PostProcessing", "Vignette", section.vignette);
        ini.SetBoolValue("PostProcessing", "VignetteSneakOnly", section.vignetteSneakOnly);
        ini.SetDoubleValue("PostProcessing", "FilmGrain", section.filmGrain);
        ini.SetValue("PostProcessing", "LUT", section.lutName.c_str());
        ini.SetDoubleValue("PostProcessing", "LUTStrength", section.lutStrength);
    }

    void ReadUpscaling(const CSimpleIniA& ini, Settings::Upscaling& section)
    {
        section.enabled = ini.GetBoolValue("Upscaling", "Enabled", section.enabled);
        section.renderScale = static_cast<float>(ini.GetDoubleValue("Upscaling", "RenderScale", section.renderScale));
    }

    void WriteUpscaling(CSimpleIniA& ini, const Settings::Upscaling& section)
    {
        ini.SetBoolValue("Upscaling", "Enabled", section.enabled);
        ini.SetDoubleValue("Upscaling", "RenderScale", section.renderScale);
    }



    void ReadSoftShadows(const CSimpleIniA& ini, Settings::SoftShadows& section)
    {
        section.enabled = ini.GetBoolValue("SoftShadows", "Enabled", section.enabled);
    }

    void WriteSoftShadows(CSimpleIniA& ini, const Settings::SoftShadows& section)
    {
        ini.SetBoolValue("SoftShadows", "Enabled", section.enabled);
    }

    void ReadFieldOfView(const CSimpleIniA& ini, Settings::FieldOfView& section)
    {
        section.customized = ini.KeyExists("FieldOfView", "Degrees");
        section.degrees = static_cast<float>(ini.GetDoubleValue("FieldOfView", "Degrees", section.degrees));
    }

    // No key at all until the player actually moves the slider.
    void WriteFieldOfView(CSimpleIniA& ini, const Settings::FieldOfView& section)
    {
        if (section.customized)
            ini.SetDoubleValue("FieldOfView", "Degrees", section.degrees);
    }

    void ReadAccessibility(const CSimpleIniA& ini, Settings::Accessibility& section)
    {
        section.colorblindMode =
            static_cast<std::uint32_t>(ini.GetLongValue("Accessibility", "ColorblindMode", section.colorblindMode));
        section.colorblindStrength =
            static_cast<float>(ini.GetDoubleValue("Accessibility", "ColorblindStrength", section.colorblindStrength));
    }

    void WriteAccessibility(CSimpleIniA& ini, const Settings::Accessibility& section)
    {
        ini.SetLongValue("Accessibility", "ColorblindMode", static_cast<long>(section.colorblindMode));
        ini.SetDoubleValue("Accessibility", "ColorblindStrength", section.colorblindStrength);
    }

    void ReadReflex(const CSimpleIniA& ini, Settings::Reflex& section)
    {
        section.enabled = ini.GetBoolValue("Reflex", "Enabled", section.enabled);
        section.lowLatencyMode = ini.GetBoolValue("Reflex", "LowLatencyMode", section.lowLatencyMode);
        section.lowLatencyBoost = ini.GetBoolValue("Reflex", "LowLatencyBoost", section.lowLatencyBoost);
        section.useMarkersToOptimize = ini.GetBoolValue("Reflex", "UseMarkersToOptimize", section.useMarkersToOptimize);
        section.useFpsLimit = ini.GetBoolValue("Reflex", "UseFPSLimit", section.useFpsLimit);
        section.fpsLimit = static_cast<float>(ini.GetDoubleValue("Reflex", "FPSLimit", section.fpsLimit));
    }

    void WriteReflex(CSimpleIniA& ini, const Settings::Reflex& section)
    {
        ini.SetBoolValue("Reflex", "Enabled", section.enabled);
        ini.SetBoolValue("Reflex", "LowLatencyMode", section.lowLatencyMode);
        ini.SetBoolValue("Reflex", "LowLatencyBoost", section.lowLatencyBoost);
        ini.SetBoolValue("Reflex", "UseMarkersToOptimize", section.useMarkersToOptimize);
        ini.SetBoolValue("Reflex", "UseFPSLimit", section.useFpsLimit);
        ini.SetDoubleValue("Reflex", "FPSLimit", section.fpsLimit);
    }

    // The ini is hand-editable, so clamp everything to the ranges the menu
    // and the shaders expect.
    void Validate(Settings& settings)
    {
        auto& aa = settings.antiAliasing;
        aa.method = std::min(aa.method, 2u);
        aa.mipLodBias = std::clamp(aa.mipLodBias, -3.0f, 0.0f);

        auto& pp = settings.postProcessing;
        pp.sharpening = std::clamp(pp.sharpening, 0.0f, 1.0f);
        pp.exposure = std::clamp(pp.exposure, -2.0f, 2.0f);
        pp.contrast = std::clamp(pp.contrast, 0.5f, 2.0f);
        pp.saturation = std::clamp(pp.saturation, 0.0f, 2.0f);
        pp.bloomIntensity = std::clamp(pp.bloomIntensity, 0.0f, 1.0f);
        pp.tonemapMethod = std::clamp(pp.tonemapMethod, 1u, 5u);
        pp.motionBlurStrength = std::clamp(pp.motionBlurStrength, 0.0f, 1.0f);
        pp.vignette = std::clamp(pp.vignette, 0.0f, 1.0f);
        pp.filmGrain = std::clamp(pp.filmGrain, 0.0f, 1.0f);
        pp.lutStrength = std::clamp(pp.lutStrength, 0.0f, 1.0f);
        // A name, not a path.
        if (pp.lutName.find_first_of("/\\") != std::string::npos || pp.lutName.find("..") != std::string::npos) {
            logger::warn("Ignoring LUT name \"{}\" - expected a file name, not a path", pp.lutName);
            pp.lutName.clear();
        }

        settings.upscaling.renderScale = std::clamp(settings.upscaling.renderScale, 0.5f, 1.0f);

        settings.fieldOfView.degrees = std::clamp(settings.fieldOfView.degrees, 60.0f, 120.0f);

        auto& access = settings.accessibility;
        access.colorblindMode = std::min(access.colorblindMode, 4u);
        access.colorblindStrength = std::clamp(access.colorblindStrength, 0.0f, 1.0f);

        settings.reflex.fpsLimit = std::clamp(settings.reflex.fpsLimit, 30.0f, 240.0f);
    }

    // A commit also fires when the menu selection just moves to another row,
    // so most write requests carry nothing new.
    Settings g_lastWritten;
    bool     g_everWritten = false;

    void WriteIni(const Settings& settings)
    {
        if (g_everWritten && settings == g_lastWritten)
            return;

        const auto path = GetIniPath();

        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(path.string().c_str());

        WriteGeneral(ini, settings);
        ini.SetBoolValue("General", "IgnoreModConflicts", g_ignoreModConflicts);
        WriteAntiAliasing(ini, settings.antiAliasing);
        WritePostProcessing(ini, settings.postProcessing);
        WriteUpscaling(ini, settings.upscaling);
        WriteSoftShadows(ini, settings.softShadows);
        WriteFieldOfView(ini, settings.fieldOfView);
        WriteAccessibility(ini, settings.accessibility);
        WriteReflex(ini, settings.reflex);
        ini.SetBoolValue("Debug", "Enabled", g_debugEnabled);

        if (ini.SaveFile(path.string().c_str()) < 0) {
            logger::warn("Couldn't write {}", path.string());
            return;
        }

        g_lastWritten = settings;
        g_everWritten = true;
        logger::debug("Settings saved to {}", path.string());
    }
}

Settings& EditableSettings() { return g_editing; }

SettingsPtr ActiveSettings() { return g_active.load(std::memory_order_acquire); }

void PublishSettings()
{
    g_active.store(std::make_shared<const Settings>(g_editing), std::memory_order_release);
    for (auto* callback : g_publishCallbacks)
        callback();
}

void RegisterPublishCallback(void (*a_callback)())
{
    if (a_callback)
        g_publishCallbacks.push_back(a_callback);
}

void LoadSettings()
{
    const auto path = GetIniPath();

    CSimpleIniA ini;
    ini.SetUnicode();

    const bool fileExisted = ini.LoadFile(path.string().c_str()) >= 0;
    if (!fileExisted) {
        logger::warn("No settings file at {}, writing defaults", path.string());
    } else {
        ReadGeneral(ini, g_editing);
        ReadAntiAliasing(ini, g_editing.antiAliasing);
        ReadPostProcessing(ini, g_editing.postProcessing);
        ReadUpscaling(ini, g_editing.upscaling);
        ReadSoftShadows(ini, g_editing.softShadows);
        ReadFieldOfView(ini, g_editing.fieldOfView);
        ReadAccessibility(ini, g_editing.accessibility);
        ReadReflex(ini, g_editing.reflex);
        g_ignoreModConflicts = ini.GetBoolValue("General", "IgnoreModConflicts", false);
        g_debugEnabled = ini.GetBoolValue("Debug", "Enabled", false);
        Validate(g_editing);
        logger::info("Settings loaded from {}", path.string());
    }

    PublishSettings();

    if (!fileExisted)
        SaveSettings();
}

void SaveSettings()
{
    g_savePending.store(false);
    WriteIni(g_editing);
}

void RequestSaveSettings()
{
    if (!g_debounceSaves) {
        SaveSettings();
        return;
    }

    g_lastSaveRequest.store(std::chrono::steady_clock::now());
    g_savePending.store(true);
}

void UpdateSettingsSave()
{
    if (!g_savePending.load())
        return;
    if (std::chrono::steady_clock::now() - g_lastSaveRequest.load() < kSaveQuietPeriod)
        return;

    // Cleared before the read, never after: a request landing in between was
    // published before it was made, so it is already in what goes out below.
    g_savePending.store(false);

    // The snapshot, not the working copy - the menu owns g_editing.
    WriteIni(*ActiveSettings());
}

void EnableSaveDebounce() { g_debounceSaves = true; }

bool IsDebugEnabled() { return g_debugEnabled; }

bool IsIgnoringModConflicts() { return g_ignoreModConflicts; }

void SetDebugEnabled(bool a_enabled)
{
    g_debugEnabled = a_enabled;

    const auto  path = GetIniPath();
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(path.string().c_str());
    ini.SetBoolValue("Debug", "Enabled", g_debugEnabled);
    if (ini.SaveFile(path.string().c_str()) < 0)
        logger::warn("Couldn't write [Debug] Enabled to {}", path.string());
}
