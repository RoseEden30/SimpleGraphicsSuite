#include "Bloom.h"

#include "RenderPass.h"

#include <array>

namespace Bloom
{
    namespace
    {
        // Six halvings take 1080p down to 30x16, which is where the widest
        // level stops adding anything a player can see.
        constexpr std::size_t kMipCount = 6;

        // Tuned once here rather than exposed: the menu carries a single
        // intensity, the way a shipped game does. Threshold sits just under
        // 1.0 so anything brighter than white blooms, with a wide knee to keep
        // the onset from showing as a hard edge on a gradient.
        constexpr float kThreshold = 0.9f;
        constexpr float kKnee = 0.55f;

        struct ConstantsCB
        {
            float texelSize[2];
            float threshold;
            float knee;
            float uvScale[2];
            float uvMax[2];
        };
        static_assert(sizeof(ConstantsCB) == 32);

        REX::W32::ID3D11PixelShader* g_prefilterPS = nullptr;
        REX::W32::ID3D11PixelShader* g_downsamplePS = nullptr;
        REX::W32::ID3D11PixelShader* g_upsamplePS = nullptr;

        REX::W32::ID3D11SamplerState* g_sampler = nullptr;
        REX::W32::ID3D11Buffer*       g_constants = nullptr;

        // down[i] is the bright-passed chain, up[i] the widened one. Keeping
        // them apart means an upsample never reads the target it writes.
        std::array<RenderPass::Target, kMipCount> g_down;
        std::array<RenderPass::Target, kMipCount> g_up;

        bool g_initialized = false;
        bool g_failed = false;

        bool EnsureStaticResources()
        {
            if (g_initialized)
                return true;
            if (g_failed)
                return false;

            const std::filesystem::path dir = "Data/Shaders/SimpleGraphicsSuite/Bloom";
            g_prefilterPS = RenderPass::CompilePixelShader(dir / "Prefilter.hlsl");
            g_downsamplePS = RenderPass::CompilePixelShader(dir / "Downsample.hlsl");
            g_upsamplePS = RenderPass::CompilePixelShader(dir / "Upsample.hlsl");

            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            // Clamped bilinear: the chain samples past the edge on every tap,
            // and wrapping would drag the opposite side of the frame in.
            REX::W32::D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.filter = REX::W32::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            samplerDesc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.comparisonFunc = REX::W32::D3D11_COMPARISON_NEVER;
            samplerDesc.maxLOD = 3.402823466e+38F;
            device->CreateSamplerState(&samplerDesc, &g_sampler);

            REX::W32::D3D11_BUFFER_DESC cbDesc{};
            cbDesc.byteWidth = sizeof(ConstantsCB);
            cbDesc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
            cbDesc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;
            device->CreateBuffer(&cbDesc, nullptr, &g_constants);

            g_initialized = g_prefilterPS && g_downsamplePS && g_upsamplePS && g_sampler && g_constants;
            if (!g_initialized) {
                logger::error("Bloom: couldn't create the pass resources - falling back to the engine's own bloom");
                g_failed = true;
                return false;
            }

            logger::info("Bloom initialized");
            return true;
        }

        // The chain runs at half resolution and halves again per level, so a
        // level can bottom out at 1 texel on a small window.
        bool EnsureTargets(std::uint32_t a_width, std::uint32_t a_height)
        {
            std::uint32_t width = std::max(a_width / 2u, 1u);
            std::uint32_t height = std::max(a_height / 2u, 1u);

            for (std::size_t i = 0; i < kMipCount; ++i) {
                // R11G11B10 holds the overbright values the bright-pass keeps
                // at half the bandwidth of a 16-bit float target.
                if (!g_down[i].Ensure(width, height, REX::W32::DXGI_FORMAT_R11G11B10_FLOAT) ||
                    !g_up[i].Ensure(width, height, REX::W32::DXGI_FORMAT_R11G11B10_FLOAT))
                    return false;

                width = std::max(width / 2u, 1u);
                height = std::max(height / 2u, 1u);
            }
            return true;
        }

        // a_scaleX/Y are 1 for our own targets, and the engine's dynamic
        // resolution ratio for the one pass that reads its buffer.
        void SetConstants(REX::W32::ID3D11DeviceContext* a_context, std::uint32_t a_width, std::uint32_t a_height,
            float a_scaleX = 1.0f, float a_scaleY = 1.0f)
        {
            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(a_context->Map(static_cast<REX::W32::ID3D11Resource*>(g_constants), 0,
                    REX::W32::D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                return;

            const float texelX = 1.0f / static_cast<float>(a_width);
            const float texelY = 1.0f / static_cast<float>(a_height);

            auto* dst = static_cast<ConstantsCB*>(mapped.data);
            dst->texelSize[0] = texelX;
            dst->texelSize[1] = texelY;
            dst->threshold = kThreshold;
            dst->knee = kKnee;
            dst->uvScale[0] = a_scaleX;
            dst->uvScale[1] = a_scaleY;
            // A texel of margin, so the outermost of the 13 taps still lands
            // inside the region rather than on the edge of the dead area.
            dst->uvMax[0] = a_scaleX - texelX;
            dst->uvMax[1] = a_scaleY - texelY;

            a_context->Unmap(static_cast<REX::W32::ID3D11Resource*>(g_constants), 0);
        }
    }

    bool Apply(REX::W32::ID3D11ShaderResourceView* a_colorSRV, std::uint32_t a_width, std::uint32_t a_height,
        REX::W32::ID3D11ShaderResourceView** a_outResult)
    {
        if (!a_colorSRV || a_width == 0 || a_height == 0)
            return false;
        if (!EnsureStaticResources() || !EnsureTargets(a_width, a_height))
            return false;

        auto* context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;
        context->PSSetConstantBuffers(0, 1, &g_constants);

        // Bright-pass straight off the scene colour into the first level. The
        // scene only occupies part of that buffer while the render scale is
        // below 1, so the pass is confined to it.
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (auto* state = RE::BSGraphics::State::GetSingleton()) {
            const auto& runtimeData = state->GetRuntimeData();
            scaleX = std::clamp(runtimeData.dynamicResolutionWidthRatio, 0.1f, 1.0f);
            scaleY = std::clamp(runtimeData.dynamicResolutionHeightRatio, 0.1f, 1.0f);
        }

        SetConstants(context, a_width, a_height, scaleX, scaleY);
        RenderPass::Draw(g_down[0], g_prefilterPS, { a_colorSRV }, { g_sampler });

        for (std::size_t i = 1; i < kMipCount; ++i) {
            SetConstants(context, g_down[i - 1].width, g_down[i - 1].height);
            RenderPass::Draw(g_down[i], g_downsamplePS, { g_down[i - 1].srv }, { g_sampler });
        }

        // Back up the chain, widening each level into the one above it. The
        // smallest level has nothing below it, so it feeds in directly rather
        // than being copied through a pass of its own.
        for (std::size_t i = kMipCount - 1; i-- > 0;) {
            const auto& lower = (i == kMipCount - 2) ? g_down[kMipCount - 1] : g_up[i + 1];
            SetConstants(context, lower.width, lower.height);
            RenderPass::Draw(g_up[i], g_upsamplePS, { g_down[i].srv, lower.srv }, { g_sampler });
        }

        *a_outResult = g_up[0].srv;
        return true;
    }
}
