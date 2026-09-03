#include "DLSS.h"

#include "Config.h"
#include "FrameBufferCache.h"
#include "RE/BSGraphics.h"
#include "Streamline.h"

#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <sl_matrix_helpers.h>

namespace DLSS
{
    namespace
    {
        bool          g_supported = false;
        std::string   g_lastFailureReason;
        ID3D11Device* g_device = nullptr;

        const sl::ViewportHandle g_viewport{ 0u };

        sl::FrameToken* g_frameToken = nullptr;
        std::uint32_t   g_lastTokenFrame = 0xffffffff;

        bool EnsureFrameToken(std::uint32_t a_frameCount)
        {
            if (g_frameToken && g_lastTokenFrame == a_frameCount)
                return true;
            if (Streamline::GetNewFrameToken(g_frameToken) != sl::Result::eOk || !g_frameToken)
                return false;
            g_lastTokenFrame = a_frameCount;
            return true;
        }

        // Near/far plane and vertical FOV, verified against Community
        // Shaders' own Utils/Game.cpp (same relocation, same +0x40/+0x44
        // byte offsets, same horizontal-to-vertical FOV conversion).
        // Streamline flags these as invalid if left unset.
        float CameraNear() { return *REL::Relocation<float*>(RELOCATION_ID(517032, 403540), 0x40); }
        float CameraFar() { return *REL::Relocation<float*>(RELOCATION_ID(517032, 403540), 0x44); }

        // Republishes the per-frame DynamicResolutionParams cbuffer from the
        // engine's own current dynamicResolutionWidthRatio/HeightRatio -
        // verified against Community Shaders' own Upscaling.cpp, which calls
        // this exact vanilla function right after its own upscale.
        void UpdateCameraData()
        {
            using func_t = void();
            static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
            func();
        }

        float VerticalFOVRad(std::uint32_t a_width, std::uint32_t a_height)
        {
            static REL::Relocation<float*> fovDeg{ RELOCATION_ID(513786, 388785) };
            const float                    hFOVRad = *fovDeg * (3.14159265359f / 180.0f);
            const float                    unitHalfWidth = std::tan(hFOVRad / 2.0f);
            const float unitHalfHeight = unitHalfWidth / (static_cast<float>(a_width) / static_cast<float>(a_height));
            return 2.0f * std::atan(unitHalfHeight);
        }

        // sl::float4x4 uses row-vector math, the engine's captured matrices
        // use column-vector, so a transpose reconciles the two.
        sl::float4x4 ToSL(const DirectX::SimpleMath::Matrix& a_matrix)
        {
            const auto   transposed = a_matrix.Transpose();
            sl::float4x4 out{};
            std::memcpy(&out, &transposed, sizeof(out));
            return out;
        }

        // Streamline needs its history invalidated whenever Apply stops
        // running for a while - re-selecting DLAA, or coming back from a
        // menu - otherwise it reprojects against a stale frame.
        bool          g_forceReset = true;
        std::uint32_t g_lastAppliedFrame = 0;

        // Jitter/render-resolution state, set by UpdateJitter and read back by Apply.
        float         g_jitterX = 0.0f;
        float         g_jitterY = 0.0f;
        std::uint32_t g_renderWidth = 0;
        std::uint32_t g_renderHeight = 0;
        std::uint32_t g_displayWidth = 0;

        ID3D11Texture2D*          g_outputTexture = nullptr;
        ID3D11ShaderResourceView* g_outputSRV = nullptr;
        std::uint32_t             g_outputWidth = 0;
        std::uint32_t             g_outputHeight = 0;
        DXGI_FORMAT               g_outputFormat = DXGI_FORMAT_UNKNOWN;

        template <class T>
        void Release(T*& a_resource)
        {
            if (a_resource) {
                a_resource->Release();
                a_resource = nullptr;
            }
        }

        void ReleaseOutput()
        {
            Release(g_outputSRV);
            Release(g_outputTexture);
            g_outputWidth = g_outputHeight = 0;
            g_outputFormat = DXGI_FORMAT_UNKNOWN;
        }

        // Matches the color resource's own format - the result gets copied
        // straight back into that same resource (kMAIN), so CopyResource
        // requires an exact format match, not just a compatible layout.
        bool EnsureOutput(std::uint32_t a_width, std::uint32_t a_height, DXGI_FORMAT a_format)
        {
            if (g_outputTexture && g_outputWidth == a_width && g_outputHeight == a_height && g_outputFormat == a_format)
                return true;
            ReleaseOutput();

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = a_width;
            desc.Height = a_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = a_format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_outputTexture))) {
                logger::error("DLSS: couldn't create the {}x{} output texture", a_width, a_height);
                return false;
            }
            if (FAILED(g_device->CreateShaderResourceView(g_outputTexture, nullptr, &g_outputSRV))) {
                logger::error("DLSS: couldn't create the output SRV");
                ReleaseOutput();
                return false;
            }

            g_outputWidth = a_width;
            g_outputHeight = a_height;
            g_outputFormat = a_format;
            return true;
        }

        // Three textures DLSS reads through Streamline: dilated motion vectors
        // (undilated ones ghost around fast-moving edges), a reactive mask
        // from the engine's TAA mask, and a transparency mask from its
        // normal/SSR-mask target. See EncodeUpscalingTextures.hlsl.
        ID3D11ComputeShader*       g_encodeShader = nullptr;
        ID3D11Buffer*              g_encodeCB = nullptr;
        ID3D11Texture2D*           g_dilatedMV = nullptr;
        ID3D11ShaderResourceView*  g_dilatedMVSRV = nullptr;
        ID3D11UnorderedAccessView* g_dilatedMVUAV = nullptr;
        ID3D11Texture2D*           g_reactiveMask = nullptr;
        ID3D11ShaderResourceView*  g_reactiveMaskSRV = nullptr;
        ID3D11UnorderedAccessView* g_reactiveMaskUAV = nullptr;
        ID3D11Texture2D*           g_transparencyMask = nullptr;
        ID3D11ShaderResourceView*  g_transparencyMaskSRV = nullptr;
        ID3D11UnorderedAccessView* g_transparencyMaskUAV = nullptr;
        std::uint32_t              g_encodedWidth = 0;
        std::uint32_t              g_encodedHeight = 0;

        void ReleaseEncodeTextures()
        {
            Release(g_dilatedMVUAV);
            Release(g_dilatedMVSRV);
            Release(g_dilatedMV);
            Release(g_reactiveMaskUAV);
            Release(g_reactiveMaskSRV);
            Release(g_reactiveMask);
            Release(g_transparencyMaskUAV);
            Release(g_transparencyMaskSRV);
            Release(g_transparencyMask);
            g_encodedWidth = g_encodedHeight = 0;
        }

        bool EnsureEncodeShader()
        {
            if (g_encodeShader)
                return true;

            const auto path = std::filesystem::path("Data/Shaders/SimpleGraphicsSuite/EncodeUpscalingTextures.hlsl");
            ID3DBlob*  blob = nullptr;
            ID3DBlob*  errors = nullptr;
            const auto compiled = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
            if (FAILED(compiled)) {
                logger::warn("DLSS: upscaling texture encode shader compile failed: {}",
                    errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
                if (errors)
                    errors->Release();
                if (blob)
                    blob->Release();
                return false;
            }
            if (errors)
                errors->Release();

            const auto created =
                g_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_encodeShader);
            blob->Release();
            if (FAILED(created)) {
                logger::warn("DLSS: couldn't create the upscaling texture encode compute shader");
                return false;
            }
            return true;
        }

        bool EnsureEncodeResources(std::uint32_t a_width, std::uint32_t a_height)
        {
            if (!EnsureEncodeShader())
                return false;

            if (!g_encodeCB) {
                D3D11_BUFFER_DESC desc{};
                desc.ByteWidth = 16;
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(g_device->CreateBuffer(&desc, nullptr, &g_encodeCB))) {
                    logger::warn("DLSS: couldn't create the encode constant buffer");
                    return false;
                }
            }

            if (g_dilatedMV && g_encodedWidth == a_width && g_encodedHeight == a_height)
                return true;

            ReleaseEncodeTextures();

            D3D11_TEXTURE2D_DESC mvDesc{};
            mvDesc.Width = a_width;
            mvDesc.Height = a_height;
            mvDesc.MipLevels = 1;
            mvDesc.ArraySize = 1;
            mvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            mvDesc.SampleDesc.Count = 1;
            mvDesc.Usage = D3D11_USAGE_DEFAULT;
            mvDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

            if (FAILED(g_device->CreateTexture2D(&mvDesc, nullptr, &g_dilatedMV))) {
                logger::warn("DLSS: couldn't create the {}x{} dilated motion vector texture", a_width, a_height);
                return false;
            }
            if (FAILED(g_device->CreateShaderResourceView(g_dilatedMV, nullptr, &g_dilatedMVSRV)) ||
                FAILED(g_device->CreateUnorderedAccessView(g_dilatedMV, nullptr, &g_dilatedMVUAV))) {
                logger::warn("DLSS: couldn't create views for the dilated motion vector texture");
                return false;
            }

            D3D11_TEXTURE2D_DESC maskDesc = mvDesc;
            maskDesc.Format = DXGI_FORMAT_R16_FLOAT;

            if (FAILED(g_device->CreateTexture2D(&maskDesc, nullptr, &g_reactiveMask))) {
                logger::warn("DLSS: couldn't create the {}x{} reactive mask texture", a_width, a_height);
                return false;
            }
            if (FAILED(g_device->CreateShaderResourceView(g_reactiveMask, nullptr, &g_reactiveMaskSRV)) ||
                FAILED(g_device->CreateUnorderedAccessView(g_reactiveMask, nullptr, &g_reactiveMaskUAV))) {
                logger::warn("DLSS: couldn't create views for the reactive mask texture");
                return false;
            }

            if (FAILED(g_device->CreateTexture2D(&maskDesc, nullptr, &g_transparencyMask))) {
                logger::warn("DLSS: couldn't create the {}x{} transparency mask texture", a_width, a_height);
                return false;
            }
            if (FAILED(g_device->CreateShaderResourceView(g_transparencyMask, nullptr, &g_transparencyMaskSRV)) ||
                FAILED(g_device->CreateUnorderedAccessView(g_transparencyMask, nullptr, &g_transparencyMaskUAV))) {
                logger::warn("DLSS: couldn't create views for the transparency mask texture");
                return false;
            }

            g_encodedWidth = a_width;
            g_encodedHeight = a_height;
            return true;
        }

        struct EncodeResult
        {
            ID3D11ShaderResourceView* motionVectorSRV = nullptr;
            ID3D11ShaderResourceView* reactiveMaskSRV = nullptr;
            ID3D11ShaderResourceView* transparencyMaskSRV = nullptr;
        };

        // Returns nullptr SRVs (caller should fall back to the raw motion
        // vector buffer and skip the mask tags) if the pass couldn't run.
        EncodeResult EncodeUpscalingTextures(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_motionVectorSRV,
            ID3D11ShaderResourceView* a_depthSRV, ID3D11ShaderResourceView* a_taaMaskSRV,
            ID3D11ShaderResourceView* a_normalSSRMaskSRV, float a_cameraNear, float a_cameraFar, std::uint32_t a_width,
            std::uint32_t a_height)
        {
            if (!a_motionVectorSRV || !a_depthSRV || !a_taaMaskSRV || !a_normalSSRMaskSRV ||
                !EnsureEncodeResources(a_width, a_height))
                return {};

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(a_context->Map(g_encodeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                return {};
            struct
            {
                std::uint32_t width, height;
                float         cameraNear, cameraFar;
            } cbData{ a_width, a_height, a_cameraNear, a_cameraFar };
            std::memcpy(mapped.pData, &cbData, sizeof(cbData));
            a_context->Unmap(g_encodeCB, 0);

            ID3D11ShaderResourceView*  srvs[] = { a_motionVectorSRV, a_depthSRV, a_taaMaskSRV, a_normalSSRMaskSRV };
            ID3D11UnorderedAccessView* uavs[] = { g_dilatedMVUAV, g_reactiveMaskUAV, g_transparencyMaskUAV };
            a_context->CSSetShaderResources(0, _countof(srvs), srvs);
            a_context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
            a_context->CSSetConstantBuffers(0, 1, &g_encodeCB);
            a_context->CSSetShader(g_encodeShader, nullptr, 0);

            a_context->Dispatch((a_width + 7) / 8, (a_height + 7) / 8, 1);

            ID3D11UnorderedAccessView* nullUAVs[3] = { nullptr, nullptr, nullptr };
            a_context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
            ID3D11ShaderResourceView* nullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
            a_context->CSSetShaderResources(0, 4, nullSRVs);
            a_context->CSSetShader(nullptr, nullptr, 0);

            return { g_dilatedMVSRV, g_reactiveMaskSRV, g_transparencyMaskSRV };
        }
    }

    void EnsureInitialized()
    {
        g_device =
            reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder);

        Streamline::EnsureInitialized();
        g_supported = Streamline::IsAvailable() && Streamline::IsDLSSSupported();
    }

    bool IsSupported() { return g_supported; }

    void SetLastFailureReason(std::string a_reason)
    {
        if (g_lastFailureReason == a_reason)
            return;
        g_lastFailureReason = std::move(a_reason);
        if (!g_lastFailureReason.empty())
            logger::warn("DLSS: {}", g_lastFailureReason);
    }

    void UpdateJitter(RE::BSGraphics::State* a_state)
    {
        if (!g_supported) {
            SetLastFailureReason("Streamline/DLSS not initialized yet");
            return;
        }

        // DLAA never upscales - render resolution is always the display
        // resolution, so there's no dynamic-resolution ratio to compute or
        // drive here, unlike the (removed) upscaling quality modes.
        g_renderWidth = a_state->screenWidth;
        g_renderHeight = a_state->screenHeight;
        g_displayWidth = g_renderWidth;
    }

    float RecommendedMipBias()
    {
        if (!g_supported || g_renderWidth == 0 || g_displayWidth == 0)
            return 0.0f;
        return std::log2(static_cast<float>(g_renderWidth) / static_cast<float>(g_displayWidth)) - 1.0f;
    }

    namespace
    {
        // Halton(index, base): standard low-discrepancy jitter sequence used
        // by temporal upscalers (DLSS/FSR2/3) to sub-pixel-jitter the camera.
        float Halton(std::int32_t a_index, std::int32_t a_base)
        {
            float f = 1.0f, result = 0.0f;
            for (auto index = a_index; index > 0;) {
                f /= static_cast<float>(a_base);
                result += f * static_cast<float>(index % a_base);
                index = static_cast<std::int32_t>(std::floor(static_cast<float>(index) / static_cast<float>(a_base)));
            }
            return result;
        }
    }

    void ApplyProjectionJitter(RE::BSGraphics::State* a_state)
    {
        if (!g_supported || g_renderWidth == 0)
            return;

        const std::uint32_t outputWidth = a_state->screenWidth;

        // Phase count scales with the upscale ratio - the higher it is, the
        // more sub-pixel offsets are needed to fully cover the output grid.
        const float ratio = static_cast<float>(outputWidth) / static_cast<float>(std::max<std::uint32_t>(g_renderWidth, 1));
        const auto  phaseCount = std::max(8, static_cast<std::int32_t>(8.0f * ratio * ratio));
        const auto  index = (a_state->GetFrameCount() % phaseCount) + 1;
        const float x = Halton(index, 2) - 0.5f;
        const float y = Halton(index, 3) - 0.5f;

        g_jitterX = x;
        g_jitterY = y;

        a_state->projectionPosScaleX = -2.0f * x / static_cast<float>(g_renderWidth);
        a_state->projectionPosScaleY = 2.0f * y / static_cast<float>(g_renderHeight);
    }

    bool Apply(ID3D11Resource* a_colorResource, std::uint32_t a_outWidth, std::uint32_t a_outHeight)
    {
        if (!g_supported) {
            SetLastFailureReason("Streamline/DLSS not initialized yet");
            return false;
        }
        if (!a_colorResource) {
            SetLastFailureReason("no color resource passed to Apply");
            return false;
        }
        if (g_renderWidth == 0) {
            SetLastFailureReason("render resolution not set yet (waiting for UpdateJitter)");
            return false;
        }

        D3D11_TEXTURE2D_DESC mainDesc{};
        static_cast<ID3D11Texture2D*>(a_colorResource)->GetDesc(&mainDesc);

        if (IsDebugEnabled() && (a_outWidth != mainDesc.Width || a_outHeight != mainDesc.Height))
            logger::warn("[debug] DLSS: caller output size {}x{} disagreed with kMAIN's real size {}x{}", a_outWidth,
                a_outHeight, mainDesc.Width, mainDesc.Height);

        // CopyResource below requires an exact size match with a_colorResource
        // (kMAIN) - use its own real dimensions rather than trusting the
        // caller's a_outWidth/a_outHeight to already agree with them.
        const std::uint32_t outWidth = mainDesc.Width;
        const std::uint32_t outHeight = mainDesc.Height;

        if (!EnsureOutput(outWidth, outHeight, mainDesc.Format)) {
            SetLastFailureReason("couldn't create the output texture");
            return false;
        }

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        auto* context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
        auto*      state = RE::BSGraphics::State::GetSingleton();
        const auto frameCount = state ? state->GetFrameCount() : 0;

        if (!EnsureFrameToken(frameCount)) {
            SetLastFailureReason("couldn't get a Streamline frame token");
            return false;
        }

        if (frameCount != g_lastAppliedFrame + 1)
            g_forceReset = true;

        // The derived clip matrices come from sl_matrix_helpers.h's
        // recalculateCameraMatrices(). It approximates the previous frame from
        // a static, which its own comment warns about, but it is what
        // Community Shaders ships against.
        const auto& frame = FrameBufferCache::Get();

        sl::Constants constants{};
        constants.cameraAspectRatio = static_cast<float>(outWidth) / static_cast<float>(outHeight);
        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.cameraPinholeOffset = { 0.0f, 0.0f };
        constants.depthInverted = sl::Boolean::eFalse;
        constants.orthographicProjection = sl::Boolean::eFalse;
        constants.cameraFOV = VerticalFOVRad(outWidth, outHeight);
        constants.cameraNear = CameraNear();
        constants.cameraFar = CameraFar();

        const auto viewInverse = frame.cameraViewInverse.Transpose();
        constants.cameraRight = { viewInverse._11, viewInverse._12, viewInverse._13 };
        constants.cameraUp = { viewInverse._21, viewInverse._22, viewInverse._23 };
        constants.cameraFwd = { viewInverse._31, viewInverse._32, viewInverse._33 };
        constants.cameraPos = { frame.cameraPosAdjust.x, frame.cameraPosAdjust.y, frame.cameraPosAdjust.z };
        constants.cameraViewToClip = ToSL(frame.cameraProjUnjittered);

        sl::recalculateCameraMatrices(constants);

        constants.jitterOffset = { -g_jitterX, -g_jitterY };
        // The engine's native motion vectors are already close enough to
        // Streamline's expected [-1,1]-normalized range that no rescale is
        // needed - Community Shaders passes the same raw buffer with a 1:1
        // scale for its own DLSS path.
        constants.mvecScale = { 1.0f, 1.0f };
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.motionVectorsInvalidValue = FLT_MIN;
        constants.motionVectorsDilated = sl::Boolean::eFalse;
        constants.motionVectorsJittered = sl::Boolean::eFalse;
        constants.reset = g_forceReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

        if (Streamline::SetConstants(constants, *g_frameToken, g_viewport) != sl::Result::eOk) {
            SetLastFailureReason("slSetConstants failed");
            return false;
        }

        // --- DLSS options ----------------------------------------------------
        sl::DLSSOptions options{};
        options.mode = sl::DLSSMode::eDLAA;
        options.outputWidth = outWidth;
        options.outputHeight = outHeight;
        options.useAutoExposure = sl::Boolean::eTrue;
        options.preExposure = 1.0f;
        options.sharpness = 0.0f;

        // Left unset, NGX picks its own per-title default and lands
        // inconsistently. Community Shaders forces ePresetJ for DLAA on every
        // generation - same here.
        options.dlaaPreset = sl::DLSSPreset::ePresetJ;

        options.colorBuffersHDR = mainDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ? sl::Boolean::eTrue : sl::Boolean::eFalse;

        if (Streamline::DLSSSetOptions(g_viewport, options) != sl::Result::eOk) {
            SetLastFailureReason("slDLSSSetOptions failed");
            return false;
        }

        // --- Resource tags and evaluate ---------------------------------------
        auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
        auto& taaMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
        // The target ISWaterBlend.hlsl reads as "waterMaskTex" for its own
        // water history blend - the same one Community Shaders tags here.
        auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kUNDERWATER_MASK];
        auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

        const auto encoded = EncodeUpscalingTextures(context, reinterpret_cast<ID3D11ShaderResourceView*>(motionVector.SRV),
            reinterpret_cast<ID3D11ShaderResourceView*>(depth.depthSRV), reinterpret_cast<ID3D11ShaderResourceView*>(taaMask.SRV),
            reinterpret_cast<ID3D11ShaderResourceView*>(underwaterMask.SRV), CameraNear(), CameraFar(), g_renderWidth, g_renderHeight);

        sl::Resource colorInRes{ sl::ResourceType::eTex2d, a_colorResource, 0 };
        sl::Resource colorOutRes{ sl::ResourceType::eTex2d, g_outputTexture, 0 };
        sl::Resource depthRes{ sl::ResourceType::eTex2d, reinterpret_cast<ID3D11Texture2D*>(depth.texture), 0 };
        sl::Resource mvecRes{ sl::ResourceType::eTex2d,
            encoded.motionVectorSRV ? g_dilatedMV : reinterpret_cast<ID3D11Texture2D*>(motionVector.texture), 0 };
        sl::Resource reactiveRes{ sl::ResourceType::eTex2d, g_reactiveMask, 0 };
        sl::Resource transparencyRes{ sl::ResourceType::eTex2d, g_transparencyMask, 0 };

        const sl::Extent extentIn{ 0, 0, g_renderWidth, g_renderHeight };
        const sl::Extent extentOut{ 0, 0, outWidth, outHeight };

        sl::ResourceTag tags[6] = {
            { &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
            { &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
            { &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
            { &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
        };
        std::uint32_t tagCount = 4;
        if (encoded.reactiveMaskSRV)
            tags[tagCount++] = { &reactiveRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn };
        if (encoded.transparencyMaskSRV)
            tags[tagCount++] = { &transparencyRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn };

        if (Streamline::SetTag(g_viewport, tags, tagCount, context) != sl::Result::eOk) {
            SetLastFailureReason("slSetTag failed");
            return false;
        }

        const sl::BaseStructure* inputs[] = { &g_viewport };
        const auto result = Streamline::EvaluateFeature(sl::kFeatureDLSS, *g_frameToken, inputs, _countof(inputs), context);
        if (result != sl::Result::eOk) {
            SetLastFailureReason(std::format("slEvaluateFeature failed (result {})", static_cast<int>(result)));
            return false;
        }

        // Copy back into kMAIN rather than handing out a separate SRV: the
        // rest of the chain reads kMAIN directly. Marking the render target
        // dirty makes the engine rebind it instead of reusing what it had.
        context->CopyResource(a_colorResource, g_outputTexture);
        RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().stateUpdateFlags.set(
            RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

        if (IsDebugEnabled()) {
            static std::uint32_t lastRenderWidth = 0, lastRenderHeight = 0, lastOutWidth = 0, lastOutHeight = 0;
            if (g_renderWidth != lastRenderWidth || g_renderHeight != lastRenderHeight || outWidth != lastOutWidth ||
                outHeight != lastOutHeight) {
                logger::info("DLSS: applied render={}x{} -> output={}x{}, format={}", g_renderWidth, g_renderHeight,
                    outWidth, outHeight, static_cast<int>(mainDesc.Format));
                lastRenderWidth = g_renderWidth;
                lastRenderHeight = g_renderHeight;
                lastOutWidth = outWidth;
                lastOutHeight = outHeight;
            }
        }

        g_forceReset = false;
        g_lastAppliedFrame = frameCount;
        SetLastFailureReason("");
        return true;
    }
}
