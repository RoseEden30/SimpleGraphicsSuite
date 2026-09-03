#pragma once

struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;

// 3D color grading LUTs (.cube format), applied after tonemapping in the
// Vanilla HDR shader - same idea as Starfield's swappable color grading.
namespace LUT
{
    // Rescans Data/Shaders/SimpleGraphicsSuite/LUTs/*.cube. Call once at
    // startup, and again if the user wants to pick up newly dropped files
    // without restarting.
    void Rescan();

    const std::vector<std::string>& AvailableNames();

    // Loads (or returns the already-loaded) LUT by name. An empty name
    // unloads the current LUT. Returns false if a_name isn't a valid,
    // parseable .cube file - the previously loaded LUT (if any) stays active.
    bool Select(const std::string& a_name);

    ID3D11ShaderResourceView* CurrentSRV();
    ID3D11SamplerState*       Sampler();
    std::uint32_t             CurrentSize();
}
