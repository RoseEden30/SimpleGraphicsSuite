#pragma once

// Doodlum's Vanilla HDR tonemap shaders, loaded via Community Shaders'
// BSShader::LoadShaders hook, plus AMD CAS sharpening compiled into the
// same shader.
namespace PostProcessing
{
    void InstallHooks();

    // Pushes the current settings to the GPU buffer and binds (or unbinds)
    // the replaced shaders. Registered as a publish callback.
    void Reapply();

    // Debug tab support: how many of our shaders are currently active, and a
    // dev button to recompile them straight from disk (for iterating on the
    // .hlsl/.hlsli files without restarting the game).
    std::size_t ReplacedShaderCount();
    void        ReloadShadersFromDisk();

    // Whether the tonemap shader hook itself is patched in.
    bool IsInstalled();
}
