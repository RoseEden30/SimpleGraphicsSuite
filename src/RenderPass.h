#pragma once

// Full-screen-triangle render passes issued from C++: compile a pixel
// shader, bind inputs, draw. Used by the Accessibility filter.
namespace RenderPass
{
    // Same D3DCompileFromFile/CreatePixelShader pattern PostProcessing uses,
    // shared here so every pass module doesn't reimplement it.
    REX::W32::ID3D11PixelShader* CompilePixelShader(
        const std::filesystem::path& a_path, const D3D_SHADER_MACRO* a_macros = nullptr);

    // Draws a full-screen triangle (no vertex/index buffer needed - the
    // shared vertex shader generates it from SV_VertexID) with a_pixelShader,
    // binding a_srvs to t0.. and a_samplers to s0.., writing into a_rtv.
    void Draw(REX::W32::ID3D11RenderTargetView* a_rtv, std::uint32_t a_width, std::uint32_t a_height,
        REX::W32::ID3D11PixelShader* a_pixelShader,
        std::initializer_list<REX::W32::ID3D11ShaderResourceView*> a_srvs,
        std::initializer_list<REX::W32::ID3D11SamplerState*>      a_samplers);
}
