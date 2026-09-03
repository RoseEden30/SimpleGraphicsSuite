#pragma once

// Progressive downsample/upsample bloom, replacing the engine's own single
// fixed-radius blur. Dispatched from PostProcessing's SetupTechnique hook,
// just before the tonemap draw, and bound as TextureEnhancedBloom (t10).
namespace Bloom
{
    // Reads a_colorSRV (the scene colour the tonemap pass is about to use) and
    // returns the finished glow through a_outResult. False leaves everything
    // untouched, so the caller keeps the engine's own bloom.
    bool Apply(REX::W32::ID3D11ShaderResourceView* a_colorSRV, std::uint32_t a_width, std::uint32_t a_height,
        REX::W32::ID3D11ShaderResourceView** a_outResult);
}
