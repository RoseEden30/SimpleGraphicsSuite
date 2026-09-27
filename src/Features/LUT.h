#pragma once

struct ID3D11ShaderResourceView;

// 3D color grading LUTs (.cube), applied after tonemapping.
namespace LUT
{
    // Rescans Data/Shaders/SimpleGraphicsSuite/LUTs/*.cube.
    void Rescan();

    const std::vector<std::string>& AvailableNames();

    // Empty name unloads. On failure the previous LUT stays active.
    bool Select(const std::string& a_name);

    ID3D11ShaderResourceView* CurrentSRV();
    std::uint32_t             CurrentSize();
}
