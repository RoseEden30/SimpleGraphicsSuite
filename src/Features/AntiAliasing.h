#pragma once

// TAA/FXAA choice and DLAA, with texture deblur under TAA and DLAA.
namespace AntiAliasing
{
    void InstallHooks();

    // Debug tab support.
    bool IsInstalled();
}
