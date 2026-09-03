#include "Accessibility.h"

#include "Config.h"
#include "PresentHook.h"
#include "RenderPass.h"

namespace Accessibility
{
    namespace
    {
        struct SettingsCB
        {
            float colorblindMode;
            float colorblindStrength;
            float reserved0;
            float reserved1;
        };
        static_assert(sizeof(SettingsCB) == 16);

        REX::W32::ID3D11PixelShader*  g_daltonizePS = nullptr;
        REX::W32::ID3D11SamplerState* g_sampler = nullptr;
        REX::W32::ID3D11Buffer*       g_settingsBuffer = nullptr;

        // The back buffer's own contents, copied out before we overwrite it -
        // reading and writing the same resource in one draw isn't allowed.
        REX::W32::ID3D11Texture2D*          g_copyTexture = nullptr;
        REX::W32::ID3D11ShaderResourceView* g_copySRV = nullptr;
        std::uint32_t                       g_copyWidth = 0;
        std::uint32_t                       g_copyHeight = 0;
        REX::W32::DXGI_FORMAT               g_copyFormat = REX::W32::DXGI_FORMAT_UNKNOWN;

        bool EnsureStaticResources()
        {
            if (g_daltonizePS && g_sampler && g_settingsBuffer)
                return true;

            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            if (!g_daltonizePS)
                g_daltonizePS =
                    RenderPass::CompilePixelShader("Data/Shaders/SimpleGraphicsSuite/Daltonize.hlsl");

            if (!g_sampler) {
                REX::W32::D3D11_SAMPLER_DESC desc{};
                desc.filter = REX::W32::D3D11_FILTER_MIN_MAG_MIP_POINT;
                desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.comparisonFunc = REX::W32::D3D11_COMPARISON_NEVER;
                desc.maxLOD = 3.402823466e+38F;
                device->CreateSamplerState(&desc, &g_sampler);
            }

            if (!g_settingsBuffer) {
                REX::W32::D3D11_BUFFER_DESC desc{};
                desc.byteWidth = sizeof(SettingsCB);
                desc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
                desc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
                desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;
                if (FAILED(device->CreateBuffer(&desc, nullptr, &g_settingsBuffer)))
                    logger::error("Accessibility: couldn't create the settings constant buffer");
            }

            if (!g_daltonizePS)
                logger::warn("Accessibility: Daltonize shader failed to compile - colorblind filter disabled");

            return g_daltonizePS && g_sampler && g_settingsBuffer;
        }

        void ReleaseCopyTexture()
        {
            if (g_copySRV) {
                g_copySRV->Release();
                g_copySRV = nullptr;
            }
            if (g_copyTexture) {
                g_copyTexture->Release();
                g_copyTexture = nullptr;
            }
            g_copyWidth = g_copyHeight = 0;
            g_copyFormat = REX::W32::DXGI_FORMAT_UNKNOWN;
        }

        bool EnsureCopyTexture(
            REX::W32::ID3D11Device* a_device, std::uint32_t a_width, std::uint32_t a_height, REX::W32::DXGI_FORMAT a_format)
        {
            if (g_copyTexture && g_copyWidth == a_width && g_copyHeight == a_height && g_copyFormat == a_format)
                return true;

            ReleaseCopyTexture();

            REX::W32::D3D11_TEXTURE2D_DESC desc{};
            desc.width = a_width;
            desc.height = a_height;
            desc.mipLevels = 1;
            desc.arraySize = 1;
            desc.format = a_format;
            desc.sampleDesc.count = 1;
            desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
            desc.bindFlags = REX::W32::D3D11_BIND_SHADER_RESOURCE;

            if (FAILED(a_device->CreateTexture2D(&desc, nullptr, &g_copyTexture))) {
                logger::error("Accessibility: couldn't create the {}x{} back buffer copy texture", a_width, a_height);
                return false;
            }
            if (FAILED(a_device->CreateShaderResourceView(
                    static_cast<REX::W32::ID3D11Resource*>(g_copyTexture), nullptr, &g_copySRV))) {
                logger::error("Accessibility: couldn't create the back buffer copy SRV");
                ReleaseCopyTexture();
                return false;
            }

            g_copyWidth = a_width;
            g_copyHeight = a_height;
            g_copyFormat = a_format;
            return true;
        }

        // Runs right before every Present - has to stay cheap on the common
        // case (filter off), since that's every frame for most players.
        void OnPrePresent(
            REX::W32::ID3D11Device* a_device, REX::W32::ID3D11DeviceContext* a_context, REX::W32::IDXGISwapChain* a_swapChain)
        {
            const auto settings = ActiveSettings();
            if (!settings->masterEnabled || settings->accessibility.colorblindMode == 0)
                return;

            if (!EnsureStaticResources())
                return;

            REX::W32::ID3D11Texture2D* backBuffer = nullptr;
            if (FAILED(a_swapChain->GetBuffer(0, REX::W32::IID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer))) ||
                !backBuffer)
                return;

            REX::W32::D3D11_TEXTURE2D_DESC backBufferDesc{};
            backBuffer->GetDesc(&backBufferDesc);

            if (!EnsureCopyTexture(a_device, backBufferDesc.width, backBufferDesc.height, backBufferDesc.format)) {
                backBuffer->Release();
                return;
            }

            REX::W32::ID3D11RenderTargetView* backBufferRTV = nullptr;
            if (FAILED(a_device->CreateRenderTargetView(
                    static_cast<REX::W32::ID3D11Resource*>(backBuffer), nullptr, &backBufferRTV)) ||
                !backBufferRTV) {
                backBuffer->Release();
                return;
            }

            a_context->CopyResource(
                static_cast<REX::W32::ID3D11Resource*>(g_copyTexture), static_cast<REX::W32::ID3D11Resource*>(backBuffer));

            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(a_context->Map(
                    static_cast<REX::W32::ID3D11Resource*>(g_settingsBuffer), 0, REX::W32::D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                auto* dst = static_cast<SettingsCB*>(mapped.data);
                dst->colorblindMode = static_cast<float>(settings->accessibility.colorblindMode);
                dst->colorblindStrength = settings->accessibility.colorblindStrength;
                a_context->Unmap(static_cast<REX::W32::ID3D11Resource*>(g_settingsBuffer), 0);
            }
            a_context->PSSetConstantBuffers(0, 1, &g_settingsBuffer);

            RenderPass::Draw(backBufferRTV, backBufferDesc.width, backBufferDesc.height, g_daltonizePS, { g_copySRV },
                { g_sampler });

            backBufferRTV->Release();
            backBuffer->Release();
        }
    }

    void InstallHooks()
    {
        if (!PresentHook::Install()) {
            logger::error("Accessibility: couldn't install the Present hook - colorblind filter disabled");
            return;
        }
        PresentHook::RegisterPrePresent(OnPrePresent);
        logger::info("Accessibility hooks installed");
    }
}
