#pragma once

// Full-screen-triangle render passes issued from C++: create a target,
// compile a pixel shader, bind inputs, draw. Used by the bloom chain and
// the Accessibility filter.
namespace RenderPass
{
    // One render-target texture plus its views, (re)created on demand at a
    // given size - callers call Ensure() every frame, it's a no-op unless
    // the size actually changed.
    struct Target
    {
        REX::W32::ID3D11Texture2D*          texture = nullptr;
        REX::W32::ID3D11RenderTargetView*   rtv = nullptr;
        REX::W32::ID3D11ShaderResourceView* srv = nullptr;
        std::uint32_t                       width = 0;
        std::uint32_t                       height = 0;

        bool Ensure(std::uint32_t a_width, std::uint32_t a_height, REX::W32::DXGI_FORMAT a_format);
        void Release();
    };

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

    // Same, writing into a_target's own RTV - the common case for a pass
    // rendering into a texture it owns.
    inline void Draw(Target& a_target, REX::W32::ID3D11PixelShader* a_pixelShader,
        std::initializer_list<REX::W32::ID3D11ShaderResourceView*> a_srvs,
        std::initializer_list<REX::W32::ID3D11SamplerState*>      a_samplers)
    {
        Draw(a_target.rtv, a_target.width, a_target.height, a_pixelShader, a_srvs, a_samplers);
    }
}
