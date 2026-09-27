#pragma once

// TAA/FXAA choice and DLAA, with NVIDIA's mip LOD bias under DLAA.
namespace AntiAliasing
{
    void InstallHooks();

    // Debug tab support.
    bool IsInstalled();
}
