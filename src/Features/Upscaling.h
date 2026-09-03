#pragma once

// Drives the engine's own dynamic resolution ratio, so the 3D scene renders
// under native resolution. The actual FSR1 (EASU + RCAS) reconstruction runs
// in PostProcessing's shader, not here.
struct Settings;

namespace Upscaling
{
    // DLAA owns the ratio itself, and FXAA runs after the tonemap shader that
    // reconstructs, so it would re-crop the upscaled frame. PostProcessing
    // reads this too: its shader skips EASU whenever the ratio is left alone.
    bool IsActive(const Settings& a_settings);

    // Called from AntiAliasing's Main_UpdateViewport hook, right after the
    // engine's own dynamic resolution logic runs.
    void Update(RE::BSGraphics::State& a_state);
}
