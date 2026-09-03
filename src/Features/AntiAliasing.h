#pragma once

// TAA deblur / mip LOD bias, ported from doodlum's skyrim-lod-bias. Lowers
// the mip LOD bias on the game's sampler states while TAA is active, which
// sharpens the blur TAA otherwise introduces on textures.
namespace AntiAliasing
{
    void InstallHooks();

    // Debug tab support.
    bool IsInstalled();
}
