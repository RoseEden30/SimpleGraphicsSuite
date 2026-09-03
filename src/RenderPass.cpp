#include "RenderPass.h"

#include <d3dcompiler.h>

namespace RenderPass
{
    namespace
    {
        // Generates a full-screen triangle from SV_VertexID alone - no vertex
        // or index buffer needed. Standard trick: 3 vertices whose clip-space
        // positions overshoot the viewport so the triangle still covers it.
        constexpr const char* kFullScreenTriangleVS = R"(
            void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0)
            {
                uv = float2((id << 1) & 2, id & 2);
                pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
            }
        )";

        REX::W32::ID3D11VertexShader* g_fullScreenTriangleVS = nullptr;

        REX::W32::ID3D11VertexShader* GetFullScreenTriangleVS()
        {
            if (g_fullScreenTriangleVS)
                return g_fullScreenTriangleVS;

            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            ID3DBlob* shaderBlob = nullptr;
            ID3DBlob* errors = nullptr;
            const auto compiled = D3DCompile(kFullScreenTriangleVS, strlen(kFullScreenTriangleVS), "RenderPass_VS",
                nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &errors);

            if (FAILED(compiled)) {
                logger::error("RenderPass: full-screen triangle VS failed to compile: {}",
                    errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
                if (errors)
                    errors->Release();
                if (shaderBlob)
                    shaderBlob->Release();
                return nullptr;
            }
            if (errors)
                errors->Release();

            device->CreateVertexShader(
                shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &g_fullScreenTriangleVS);
            shaderBlob->Release();

            return g_fullScreenTriangleVS;
        }
    }

    REX::W32::ID3D11PixelShader* CompilePixelShader(
        const std::filesystem::path& a_path, const D3D_SHADER_MACRO* a_macros)
    {
        auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

        ID3DBlob* shaderBlob = nullptr;
        ID3DBlob* errors = nullptr;
        const auto compiled = D3DCompileFromFile(a_path.c_str(), a_macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
            "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &errors);

        if (FAILED(compiled)) {
            logger::warn("RenderPass: shader compile failed for {}: {}", a_path.string(),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
            if (errors)
                errors->Release();
            if (shaderBlob)
                shaderBlob->Release();
            return nullptr;
        }
        if (errors)
            errors->Release();

        REX::W32::ID3D11PixelShader* shader = nullptr;
        const auto created =
            device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader);
        shaderBlob->Release();

        if (FAILED(created)) {
            logger::warn("RenderPass: pixel shader creation failed for {}", a_path.string());
            return nullptr;
        }
        return shader;
    }

    void Draw(REX::W32::ID3D11RenderTargetView* a_rtv, std::uint32_t a_width, std::uint32_t a_height,
        REX::W32::ID3D11PixelShader* a_pixelShader,
        std::initializer_list<REX::W32::ID3D11ShaderResourceView*> a_srvs,
        std::initializer_list<REX::W32::ID3D11SamplerState*>      a_samplers)
    {
        auto* vs = GetFullScreenTriangleVS();
        if (!vs || !a_pixelShader || !a_rtv)
            return;

        auto* context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

        REX::W32::D3D11_VIEWPORT viewport{};
        viewport.width = static_cast<float>(a_width);
        viewport.height = static_cast<float>(a_height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        context->OMSetRenderTargets(1, &a_rtv, nullptr);

        context->VSSetShader(vs, nullptr, 0);
        context->PSSetShader(a_pixelShader, nullptr, 0);
        context->IASetPrimitiveTopology(REX::W32::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (a_srvs.size() > 0)
            context->PSSetShaderResources(0, static_cast<std::uint32_t>(a_srvs.size()), a_srvs.begin());
        if (a_samplers.size() > 0)
            context->PSSetSamplers(0, static_cast<std::uint32_t>(a_samplers.size()), a_samplers.begin());

        context->Draw(3, 0);
    }
}
