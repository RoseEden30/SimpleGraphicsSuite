#pragma once

#include <memory>

// One INI, one section per module. The render thread (UI) edits a working
// copy and publishes an immutable snapshot; the hooks, which run on other
// threads, read the snapshot without locking.
struct Settings
{
    bool masterEnabled = true;  // overrides every module's own toggle when off

    struct AntiAliasing
    {
        // 0=TAA, 1=FXAA, 2=DLAA (ours) - mutually exclusive, picking one forces
        // the others off (TAA and FXAA are the game's own).
        std::uint32_t method = 0;

        bool  enabled = true;
        // TAA deblur: negative sharpens texture sampling under TAA. -0.9 out
        // of a -3.0 max lands on a clean 30%, matching the 10%-step slider;
        // the original mod's own default is -1.0.
        float mipLodBias = -0.9f;

        bool operator==(const AntiAliasing&) const = default;
    } antiAliasing;

    struct PostProcessing
    {
        bool  enabled = true;
        float sharpening = 0.0f;  // AMD FidelityFX RCAS strength, 0.0-1.0

        // Match doodlum's own defaults in VanillaHDRSettings.fxh.
        float exposure = 0.0f;    // -2.0 to 2.0
        float contrast = 1.39f;   // 0.5 to 2.0
        float saturation = 1.0f;  // 0.0 to 2.0
        float bloomIntensity = 0.5f;  // 0.0-1.0, 0=off

        // 1=channel, 2=peak, 3=average luma, 4=Frostbyte hue preservation
        // (the original mod's own default), 5=ACES.
        std::uint32_t tonemapMethod = 4;

        float motionBlurStrength = 0.0f;  // 0.0-1.0, 0=off

        float vignette = 0.0f;  // 0.0-1.0, 0=off
        // Only apply the vignette while the player is sneaking, instead of
        // all the time - a stealth-game visual cue.
        bool vignetteSneakOnly = false;

        // 3D color grading LUT (.cube), scanned from
        // Data/Shaders/SimpleGraphicsSuite/LUTs/ - empty name means none.
        std::string lutName;
        float       lutStrength = 1.0f;  // 0.0-1.0

        bool operator==(const PostProcessing&) const = default;
    } postProcessing;

    struct Upscaling
    {
        bool  enabled = false;
        float renderScale = 1.0f;  // 0.5 to 1.0 - fraction of native resolution to render at

        bool operator==(const Upscaling&) const = default;
    } upscaling;


    struct SoftShadows
    {
        bool enabled = false;

        bool operator==(const SoftShadows&) const = default;
    } softShadows;

    struct FieldOfView
    {
        // True once the player has picked a value - until then the game's
        // own FOV is left untouched.
        bool  customized = false;
        float degrees = 75.0f;  // 60 to 120

        bool operator==(const FieldOfView&) const = default;
    } fieldOfView;

    struct Accessibility
    {
        // 0=Off, 1=Protanopia, 2=Deuteranopia, 3=Tritanopia, 4=Grayscale (debug).
        std::uint32_t colorblindMode = 0;
        float         colorblindStrength = 1.0f;  // 0.0-1.0

        bool operator==(const Accessibility&) const = default;
    } accessibility;

    struct Reflex
    {
        bool  enabled = true;
        bool  lowLatencyMode = true;
        // NVIDIA's own docs note Boost trades power/FPS for a marginal extra
        // latency cut, so it opts in rather than being on by default.
        bool  lowLatencyBoost = false;
        bool  useMarkersToOptimize = false;
        bool  useFpsLimit = false;
        float fpsLimit = 60.0f;

        bool operator==(const Reflex&) const = default;
    } reflex;

    bool operator==(const Settings&) const = default;
};

using SettingsPtr = std::shared_ptr<const Settings>;

// Working copy. Render thread only.
Settings& EditableSettings();

// Snapshot for the hooks, valid as long as the caller keeps the pointer.
SettingsPtr ActiveSettings();

// Publishes the working copy as the new active snapshot, then runs every
// callback registered via RegisterPublishCallback.
void PublishSettings();

// Runs a_callback after every PublishSettings call, so a module caching
// settings in a GPU buffer never needs its callers to reapply by hand. Call
// once, from InstallHooks.
void RegisterPublishCallback(void (*a_callback)());

// Writes the default ini if none exists, reads it, clamps it and publishes.
void LoadSettings();

// Writes the working copy back to the ini, there and then.
void SaveSettings();

// Same write, once the changes stop coming. Publishing stays immediate.
void RequestSaveSettings();

// Completes a pending request once its quiet period has passed. Meant to run
// every frame - nothing else finishes a deferred write.
void UpdateSettingsSave();

// Lets RequestSaveSettings defer. Call once, alongside whatever ticks
// UpdateSettingsSave; without it, requests are written straight away.
void EnableSaveDebounce();

// [Debug] Enabled - a dev flag kept outside the Settings struct, so "Reset to
// defaults" never clears it. Read once at startup: the panels it gates are
// registered then, so a change only shows up on the next launch.
bool IsDebugEnabled();

// Updates the flag and writes it straight to the ini, without touching the
// rest of the settings.
void SetDebugEnabled(bool a_enabled);

// [General] IgnoreModConflicts - kept outside Settings for the same reason as
// the debug flag. Read once at startup, so a change needs a restart.
bool IsIgnoringModConflicts();
