#pragma once

// Doodlum's Vanilla HDR tonemap shaders with the suite's effects, swapped in
// through a BSShader::LoadShaders hook.
namespace PostProcessing
{
    void InstallHooks();

    // Applied on the next draw. Registered as a publish callback.
    void Reapply();

    // Debug tab support.
    std::size_t ReplacedShaderCount();
    void        ReloadShadersFromDisk();

    // Whether the tonemap shader hook itself is patched in.
    bool IsInstalled();
}
