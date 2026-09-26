#pragma once

// Full-screen-triangle render passes issued from C++: compile a pixel
// shader, bind inputs, draw. Used by the Accessibility filter.
namespace RenderPass
{
    REX::W32::ID3D11PixelShader* CompilePixelShader(
        const std::filesystem::path& a_path, const D3D_SHADER_MACRO* a_macros = nullptr);

    // Draws a full-screen triangle (no vertex/index buffer needed - the
    // shared vertex shader generates it from SV_VertexID) with a_pixelShader,
    // binding a_srvs to t0.. and a_samplers to s0.., writing into a_rtv.
    void Draw(REX::W32::ID3D11RenderTargetView* a_rtv, std::uint32_t a_width, std::uint32_t a_height,
        REX::W32::ID3D11PixelShader* a_pixelShader,
        std::initializer_list<REX::W32::ID3D11ShaderResourceView*> a_srvs,
        std::initializer_list<REX::W32::ID3D11SamplerState*>      a_samplers);

    // Restores what a pass changes, so the engine's state cache stays valid.
    class StateBackup
    {
    public:
        StateBackup();
        ~StateBackup();

        StateBackup(const StateBackup&) = delete;
        StateBackup& operator=(const StateBackup&) = delete;

    private:
        ID3D11DeviceContext*      _context = nullptr;
        ID3D11RenderTargetView*   _rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        ID3D11DepthStencilView*   _dsv = nullptr;
        D3D11_VIEWPORT            _viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT                      _viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ID3D11BlendState*         _blend = nullptr;
        FLOAT                     _blendFactor[4]{};
        UINT                      _sampleMask = 0;
        ID3D11DepthStencilState*  _depthStencil = nullptr;
        UINT                      _stencilRef = 0;
        ID3D11RasterizerState*    _rasterizer = nullptr;
        ID3D11VertexShader*       _vs = nullptr;
        ID3D11PixelShader*        _ps = nullptr;
        ID3D11GeometryShader*     _gs = nullptr;
        ID3D11HullShader*         _hs = nullptr;
        ID3D11DomainShader*       _ds = nullptr;
        ID3D11InputLayout*        _inputLayout = nullptr;
        D3D11_PRIMITIVE_TOPOLOGY  _topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11ShaderResourceView* _psSRV = nullptr;
        ID3D11SamplerState*       _psSampler = nullptr;
        ID3D11Buffer*             _psCB = nullptr;
    };
}
