#include "PostProcessing.h"

#include "Config.h"
#include "VTablePatch.h"

#include "Features/DLSS.h"
#include "Features/LUT.h"
#include "Features/Bloom.h"
#include "Features/Upscaling.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string_view>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4324)
#include <xbyak/xbyak.h>
#pragma warning(pop)

namespace PostProcessing
{
    namespace
    {
        // Mirrors SimpleGraphicsSuiteSettings in Settings.hlsli. Everything
        // continuous lives here so dragging a slider never recompiles a
        // shader; only screen size stays a compile-time macro.
        struct SettingsCB
        {
            float sharpening;
            float exposure;
            float contrast;
            float saturation;
            float bloomIntensity;
            float motionBlurAmount;
            float upscalingEnabled;
            float lutStrength;
            float lutSize;
            float tonemapMethod;
            float vignette;
            float postProcessingEnabled;  // gates only the ENB grading block, see SGS_PostProcessingEnabled
            float bloomEnhanced;
            float reserved2;  // pads to a 16-byte cbuffer row - keep in sync with Settings.hlsli
            float reserved3;
            float reserved4;
        };
        static_assert(sizeof(SettingsCB) == 64);

        bool NeedsReplacedShader(const Settings& a_settings);  // defined near ApplyEnabled below

        bool IsPlayerSneaking()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && player->IsSneaking();
        }

        // Set from Hook_SetupTechnique, read by UpdateSettingsBuffer - see
        // the motion blur SRV binding below.
        bool g_motionBlurSuppressedByMenu = false;

        // Whether our own bloom chain produced this frame's TextureBloom.
        bool g_bloomEnhanced = false;

        // How long a full 0<->target vignette transition takes, fading
        // rather than snapping instantly when sneak state changes.
        constexpr float                      kVignetteFadeSeconds = 0.35f;
        float                                 g_vignetteCurrent = 0.0f;
        std::chrono::steady_clock::time_point g_vignetteLastTick = std::chrono::steady_clock::now();

        // Moves g_vignetteCurrent toward a_target at a constant rate,
        // independent of frame rate. Returns true while still transitioning
        // - the caller only needs to touch the GPU buffer in that case.
        bool StepVignette(float a_target)
        {
            const auto  now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - g_vignetteLastTick).count();
            g_vignetteLastTick = now;

            if (g_vignetteCurrent == a_target)
                return false;

            const float maxDelta = std::max(deltaSeconds, 0.0f) / kVignetteFadeSeconds;
            if (std::abs(a_target - g_vignetteCurrent) <= maxDelta)
                g_vignetteCurrent = a_target;
            else
                g_vignetteCurrent += a_target > g_vignetteCurrent ? maxDelta : -maxDelta;
            return true;
        }

        REX::W32::ID3D11Buffer* g_settingsBuffer = nullptr;

        void EnsureSettingsBuffer()
        {
            if (g_settingsBuffer)
                return;

            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            REX::W32::D3D11_BUFFER_DESC desc{};
            desc.byteWidth = sizeof(SettingsCB);
            desc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
            desc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
            desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;

            if (FAILED(device->CreateBuffer(&desc, nullptr, &g_settingsBuffer)))
                logger::error("Post-processing: couldn't create the settings constant buffer");
        }

        void UpdateSettingsBuffer(const Settings& a_settings)
        {
            EnsureSettingsBuffer();
            if (!g_settingsBuffer)
                return;

            const auto& postProcessing = a_settings.postProcessing;

            LUT::Select(postProcessing.lutName);

            auto* context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(static_cast<REX::W32::ID3D11Resource*>(g_settingsBuffer), 0,
                    REX::W32::D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                return;

            auto* dst = static_cast<SettingsCB*>(mapped.data);
            dst->sharpening = postProcessing.sharpening;
            dst->exposure = postProcessing.exposure;
            dst->contrast = postProcessing.contrast;
            dst->saturation = postProcessing.saturation;
            dst->bloomIntensity = postProcessing.bloomIntensity;
            dst->motionBlurAmount = g_motionBlurSuppressedByMenu ? 0.0f : postProcessing.motionBlurStrength;
            dst->upscalingEnabled = Upscaling::IsActive(a_settings) ? 1.0f : 0.0f;
            dst->lutStrength = LUT::CurrentSRV() ? postProcessing.lutStrength : 0.0f;
            dst->lutSize = static_cast<float>(LUT::CurrentSize());
            dst->tonemapMethod = static_cast<float>(postProcessing.tonemapMethod);
            // Sneak-only mode fades g_vignetteCurrent toward the target
            // instead of snapping - see StepVignette, driven per-frame from
            // Hook_SetupTechnique.
            dst->vignette = postProcessing.vignetteSneakOnly ? g_vignetteCurrent : postProcessing.vignette;
            dst->postProcessingEnabled = postProcessing.enabled ? 1.0f : 0.0f;
            // Set by ApplyBloom once it knows the chain actually ran.
            dst->bloomEnhanced = g_bloomEnhanced ? 1.0f : 0.0f;

            context->Unmap(static_cast<REX::W32::ID3D11Resource*>(g_settingsBuffer), 0);
        }

        REX::W32::ID3D11PixelShader* CompilePixelShader(const std::filesystem::path& a_path)
        {
            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
            const auto invWidthStr = std::format("{}", 1.0f / static_cast<float>(screenSize.width));
            const auto invHeightStr = std::format("{}", 1.0f / static_cast<float>(screenSize.height));

            // Trailing entries stay zeroed and terminate the list.
            D3D_SHADER_MACRO macros[6] = {
                { "WINPC", "" },
                { "DX11", "" },
                { "SCREEN_INV_WIDTH", invWidthStr.c_str() },
                { "SCREEN_INV_HEIGHT", invHeightStr.c_str() },
            };
            // The PerFrame buffer at b12 has a stereo layout in VR.
            if (REL::Module::IsVR())
                macros[4] = { "VR", "" };

            ID3DBlob* shaderBlob = nullptr;
            ID3DBlob* errors = nullptr;

            const auto compiled = D3DCompileFromFile(a_path.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &errors);

            if (FAILED(compiled)) {
                logger::warn("Shader compile failed for {}: {}", a_path.string(),
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
                logger::warn("Pixel shader creation failed for {}", a_path.string());
                return nullptr;
            }
            return shader;
        }

        // Files are named <technique ID in hex>.ps.hlsl, the layout doodlum
        // ships Vanilla HDR's shaders in.
        std::unordered_map<std::uint32_t, std::filesystem::path> ScanShaderFolder(const std::filesystem::path& a_dir)
        {
            std::unordered_map<std::uint32_t, std::filesystem::path> found;
            if (!std::filesystem::exists(a_dir))
                return found;

            for (const auto& entry : std::filesystem::directory_iterator(a_dir)) {
                const auto name = entry.path().filename().string();
                if (!name.ends_with(".ps.hlsl"))
                    continue;

                const auto idPart = name.substr(0, name.size() - 8);
                const auto techniqueId = static_cast<std::uint32_t>(std::strtoul(idPart.c_str(), nullptr, 16));
                found.emplace(techniqueId, entry.path());
            }
            return found;
        }

        struct ReplacedShader
        {
            RE::BSGraphics::PixelShader*  entry;
            REX::W32::ID3D11PixelShader*  original;
            REX::W32::ID3D11PixelShader*  replaced;
            std::filesystem::path         path;
        };

        std::vector<ReplacedShader> g_replaced;

        // The tonemap techniques - our own passes run just before this draw.
        constexpr std::array kTonemapShaderNames = { "ISHDRTonemapBlendCinematic"sv, "ISHDRTonemapBlendCinematicFade"sv };

        using SetupTechnique_t = bool (*)(RE::BSShader*, std::uint32_t);
        std::unordered_map<void*, SetupTechnique_t> g_patchedVtables;

        // Our passes necessarily change RTV/viewport/shaders/SRVs/samplers, and
        // the tonemap draw that follows has to see the engine's own state back.
        struct SavedPipelineState
        {
            REX::W32::ID3D11DeviceContext*      context;
            REX::W32::ID3D11RenderTargetView*   rtv = nullptr;
            REX::W32::ID3D11DepthStencilView*   dsv = nullptr;
            REX::W32::D3D11_VIEWPORT            viewport{};
            REX::W32::ID3D11VertexShader*       vs = nullptr;
            REX::W32::ID3D11PixelShader*        ps = nullptr;
            REX::W32::D3D11_PRIMITIVE_TOPOLOGY  topology{};
            REX::W32::ID3D11ShaderResourceView* srvs[3]{};
            REX::W32::ID3D11SamplerState*       samplers[2]{};

            explicit SavedPipelineState(REX::W32::ID3D11DeviceContext* a_context) :
                context(a_context)
            {
                context->OMGetRenderTargets(1, &rtv, &dsv);
                std::uint32_t viewportCount = 1;
                context->RSGetViewports(&viewportCount, &viewport);
                context->VSGetShader(&vs, nullptr, nullptr);
                context->PSGetShader(&ps, nullptr, nullptr);
                context->IAGetPrimitiveTopology(&topology);
                context->PSGetShaderResources(0, 3, srvs);
                context->PSGetSamplers(0, 2, samplers);
            }

            ~SavedPipelineState()
            {
                context->OMSetRenderTargets(1, &rtv, dsv);
                context->RSSetViewports(1, &viewport);
                context->VSSetShader(vs, nullptr, 0);
                context->PSSetShader(ps, nullptr, 0);
                context->IASetPrimitiveTopology(topology);
                context->PSSetShaderResources(0, 3, srvs);
                context->PSSetSamplers(0, 2, samplers);

                if (rtv)
                    rtv->Release();
                if (dsv)
                    dsv->Release();
                if (vs)
                    vs->Release();
                if (ps)
                    ps->Release();
                for (auto* srv : srvs)
                    if (srv)
                        srv->Release();
                for (auto* sampler : samplers)
                    if (sampler)
                        sampler->Release();
            }

            SavedPipelineState(const SavedPipelineState&) = delete;
            SavedPipelineState& operator=(const SavedPipelineState&) = delete;
        };

        // Builds the glow off the colour the tonemap draw is about to read and
        // binds it over TextureBloom (t0), in place of the engine's own.
        void ApplyBloom(REX::W32::ID3D11DeviceContext* a_context, const Settings& a_settings)
        {
            const bool wanted =
                a_settings.masterEnabled && a_settings.postProcessing.enabled &&
                a_settings.postProcessing.bloomIntensity > 0.0f;
            if (!wanted) {
                if (g_bloomEnhanced) {
                    g_bloomEnhanced = false;
                    UpdateSettingsBuffer(a_settings);
                }
                return;
            }

            // Straight off kMAIN rather than whatever sits at t1: the engine
            // binds its textures after SetupTechnique returns, so reading the
            // pipeline here gets the previous draw's state.
            auto* colorSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(
                RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV);
            if (!colorSRV) {
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    logger::warn("Bloom: kMAIN has no shader resource view - keeping the engine's own bloom");
                    loggedOnce = true;
                }
                return;
            }

            const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();

            REX::W32::ID3D11ShaderResourceView* result = nullptr;
            bool                                applied = false;
            {
                SavedPipelineState saved{ a_context };
                applied = Bloom::Apply(colorSRV, screenSize.width, screenSize.height, &result);
            }

            // The shader only skips the imagespace's own gate once the chain
            // has actually produced something.
            if (applied != g_bloomEnhanced) {
                g_bloomEnhanced = applied;
                UpdateSettingsBuffer(a_settings);
            }

            if (applied && result) {
                a_context->PSSetShaderResources(10, 1, &result);
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    logger::info("Bloom: applied successfully");
                    loggedOnce = true;
                }
            } else if (!applied) {
                static bool loggedFailureOnce = false;
                if (!loggedFailureOnce) {
                    logger::warn("Bloom: enabled but failed to apply - see earlier Bloom errors in the log");
                    loggedFailureOnce = true;
                }
            }
        }

        // Runs before the vanilla chain (bloom, SAO, tonemap) starts, since
        // those read kMAIN directly and need the resolved, de-jittered result.
        // Substituting a texture at tonemap time instead leaves every earlier
        // pass on the jittered buffer, which shimmers.
        void ApplyDLSS()
        {
            const auto settings = ActiveSettings();
            if (!settings->masterEnabled || !settings->antiAliasing.enabled || settings->antiAliasing.method != 2)
                return;

            auto& main = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
            if (!main.texture) {
                DLSS::SetLastFailureReason("kMAIN render target has no texture yet");
                return;
            }

            const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
            const bool applied =
                DLSS::Apply(reinterpret_cast<ID3D11Resource*>(main.texture), screenSize.width, screenSize.height);

            if (applied) {
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    logger::info("DLSS: applied successfully");
                    loggedOnce = true;
                }
            } else {
                static bool loggedFailureOnce = false;
                if (!loggedFailureOnce) {
                    logger::warn("DLSS: enabled but failed to apply - see earlier DLSS errors in the log");
                    loggedFailureOnce = true;
                }
            }
        }

        // Runs on every technique draw for a patched shader class, so it has
        // to stay cheap: a handful of string compares, no allocation.
        bool Hook_SetupTechnique(RE::BSShader* a_this, std::uint32_t a_technique)
        {
            const auto vtable = *reinterpret_cast<void**>(a_this);
            const auto it = g_patchedVtables.find(vtable);
            const bool result = it != g_patchedVtables.end() ? it->second(a_this, a_technique) : false;

            auto& runtimeData = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData();

            if (g_settingsBuffer)
                runtimeData.context->PSSetConstantBuffers(13, 1, &g_settingsBuffer);

            const bool isTonemapShader = a_this->fxpFilename &&
                std::ranges::find(kTonemapShaderNames, std::string_view{ a_this->fxpFilename }) !=
                    kTonemapShaderNames.end();
            if (isTonemapShader) {
                // One snapshot for the whole draw - every ActiveSettings()
                // load locks, and this runs per technique setup.
                const auto  settingsPtr = ActiveSettings();
                const auto& settings = *settingsPtr;

                ApplyBloom(runtimeData.context, settings);

                if (auto* lutSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(LUT::CurrentSRV())) {
                    auto* lutSampler = reinterpret_cast<REX::W32::ID3D11SamplerState*>(LUT::Sampler());
                    runtimeData.context->PSSetShaderResources(8, 1, &lutSRV);
                    runtimeData.context->PSSetSamplers(8, 1, &lutSampler);
                }

                // Motion vector + depth for per-object motion blur, see
                // MotionBlur.hlsli. Suppressed while a menu has the game
                // paused: the map and the wait menu move the scene far more
                // than gameplay does, which reads as smearing.
                {
                    auto*      ui = RE::UI::GetSingleton();
                    const bool pausedByMenu = ui && ui->GameIsPaused();
                    if (pausedByMenu != g_motionBlurSuppressedByMenu) {
                        g_motionBlurSuppressedByMenu = pausedByMenu;
                        UpdateSettingsBuffer(settings);
                    }

                    if (settings.masterEnabled && settings.postProcessing.motionBlurStrength > 0.0f && !pausedByMenu) {
                        auto* motionSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(
                            runtimeData.renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV);
                        runtimeData.context->PSSetShaderResources(7, 1, &motionSRV);

                        auto& depthStencils = RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData();
                        auto* depthSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(
                            depthStencils.depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN].depthSRV);
                        runtimeData.context->PSSetShaderResources(9, 1, &depthSRV);
                    }
                }

                // Fades in and out on a sneak/stand transition instead of
                // snapping. The settings buffer is only remapped while
                // actually transitioning.
                if (settings.postProcessing.vignetteSneakOnly) {
                    const float target = IsPlayerSneaking() ? settings.postProcessing.vignette : 0.0f;
                    if (StepVignette(target))
                        UpdateSettingsBuffer(settings);
                } else {
                    g_vignetteCurrent = settings.postProcessing.vignette;
                }
            }

            return result;
        }

        // BSShader::SetupTechnique is virtual - shaders sharing the same
        // underlying C++ class share one vtable, so this only needs patching
        // once per distinct class, not once per technique file.
        void PatchSetupTechnique(RE::BSShader& a_shader)
        {
            auto* vtable = *reinterpret_cast<void**>(&a_shader);
            if (g_patchedVtables.contains(vtable))
                return;

            SetupTechnique_t original = nullptr;
            if (!VTablePatch::PatchSlot(&a_shader, 2, reinterpret_cast<void*>(&Hook_SetupTechnique),
                    reinterpret_cast<void**>(&original))) {
                logger::warn("Post-processing: couldn't hook SetupTechnique for {}", a_shader.fxpFilename);
                return;
            }
            g_patchedVtables.emplace(vtable, original);
        }

        void ReplaceShaders(RE::BSShader& a_shader)
        {
            const auto shaderDir = std::filesystem::path("Data/Shaders") / a_shader.fxpFilename;
            const auto found = ScanShaderFolder(shaderDir);
            if (found.empty())
                return;

            const auto activeSettings = ActiveSettings();
            const bool enabledNow = NeedsReplacedShader(*activeSettings);
            UpdateSettingsBuffer(*activeSettings);

            std::size_t replaced = 0;
            for (const auto& entry : a_shader.pixelShaders) {
                const auto it = found.find(entry->id);
                if (it == found.end())
                    continue;

                auto* compiled = CompilePixelShader(it->second);
                if (!compiled)
                    continue;

                g_replaced.push_back({ entry, entry->shader, compiled, it->second });
                if (enabledNow)
                    entry->shader = compiled;
                ++replaced;
            }

            // Every one of our shaders reads the settings cbuffer, so all of
            // them need the SetupTechnique hook, not just the motion blur ones.
            PatchSetupTechnique(a_shader);

            if (replaced > 0)
                logger::info("{}: compiled {} of {} pixel shader(s) from {}", a_shader.fxpFilename, replaced,
                    found.size(), shaderDir.string());
        }

        using LoadShaders_t = void (*)(RE::BSShader*, std::uintptr_t);
        LoadShaders_t g_originalLoadShaders = nullptr;

        void Hook_LoadShaders(RE::BSShader* a_shader, std::uintptr_t a_stream)
        {
            g_originalLoadShaders(a_shader, a_stream);
            ReplaceShaders(*a_shader);
        }

        // The call site patched below sits inside the engine's own
        // post-processing entry point (bloom, SAO, tonemap all run inside
        // it) - running DLSS here, before that call, matches Community
        // Shaders' own upscale ordering. See ApplyDLSS.
        using MainPostProcessing_t = void (*)(RE::ImageSpaceManager*, std::uint32_t, RE::RENDER_TARGET, void*, bool);
        MainPostProcessing_t g_originalMainPostProcessing = nullptr;

        void Hook_MainPostProcessing(
            RE::ImageSpaceManager* a_this, std::uint32_t a3, RE::RENDER_TARGET a_target, void* a4, bool a5)
        {
            ApplyDLSS();
            g_originalMainPostProcessing(a_this, a3, a_target, a4, a5);
        }

        // Patches the function entry directly (write_branch can't relocate a
        // clobbered prologue on its own). Offsets from doodlum's SSEShaderTools.
        struct Patch : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_originalFunc)
            {
                Xbyak::Label nonNullLabel;

                test(rdx, rdx);
                jz(nonNullLabel);
                jmp(ptr[rip]);
                dq(a_originalFunc + 0x9);

                L(nonNullLabel);
                jmp(ptr[rip]);
                dq(a_originalFunc + 0xF0);
            }
        };

        // Any effect living in the replaced shader has to keep it bound, not
        // just the grading toggle. Upscaling counts: its FSR1 EASU
        // reconstruction is in that same shader.
        bool NeedsReplacedShader(const Settings& a_settings)
        {
            if (!a_settings.masterEnabled)
                return false;

            const auto& pp = a_settings.postProcessing;
            return pp.enabled || pp.motionBlurStrength > 0.0f || pp.vignette > 0.0f || pp.sharpening > 0.0f ||
                !pp.lutName.empty() ||
                a_settings.upscaling.enabled;
        }

        // -1 until the first apply, so that one always goes through.
        int g_loggedEnabled = -1;

        void ApplyEnabled(bool a_enabled)
        {
            if (g_loggedEnabled != static_cast<int>(a_enabled)) {
                g_loggedEnabled = a_enabled;
                logger::info("Post-processing: active={}", a_enabled);
            }
            for (const auto& shader : g_replaced)
                shader.entry->shader = a_enabled ? shader.replaced : shader.original;
        }
    }

    void Reapply()
    {
        const auto settings = ActiveSettings();
        UpdateSettingsBuffer(*settings);
        ApplyEnabled(NeedsReplacedShader(*settings));
    }

    std::size_t ReplacedShaderCount() { return g_replaced.size(); }

    bool IsInstalled() { return !g_patchedVtables.empty(); }

    // Recompiles every replaced shader straight from its .hlsl/.hlsli files
    // on disk - a dev convenience for iterating on the shader source without
    // restarting the game. Not used by the normal settings-change path
    // anymore, since those live in the settings buffer instead.
    void ReloadShadersFromDisk()
    {
        const bool  bindNow = NeedsReplacedShader(*ActiveSettings());
        std::size_t ok = 0;
        std::size_t failed = 0;

        for (auto& shader : g_replaced) {
            auto* recompiled = CompilePixelShader(shader.path);
            if (!recompiled) {
                ++failed;
                continue;
            }

            auto* previous = shader.replaced;
            shader.replaced = recompiled;
            if (bindNow)
                shader.entry->shader = recompiled;
            if (previous)
                previous->Release();
            ++ok;
        }

        logger::info("Post-processing: reloaded {} of {} shader(s) from disk ({} failed)", ok, g_replaced.size(),
            failed);
    }

    void InstallHooks()
    {
        const auto target = RELOCATION_ID(101339, 108326).address();

        Patch patch{ target };
        patch.ready();

        auto& trampoline = SKSE::GetTrampoline();
        g_originalLoadShaders = reinterpret_cast<LoadShaders_t>(trampoline.allocate(patch));
        trampoline.write_branch<6>(target, Hook_LoadShaders);

        // Call site inside the engine's own post-processing entry point -
        // verified against alandtse's open-shaders (their Main_PostProcessing,
        // same RelocationID), VR offset included.
        const auto postProcessingCall =
            RELOCATION_ID(100430, 107148).address() + REL::Relocate<std::uintptr_t>(0x1F0, 0x1E7, 0x206);
        g_originalMainPostProcessing =
            reinterpret_cast<MainPostProcessing_t>(trampoline.write_call<5>(postProcessingCall, &Hook_MainPostProcessing));

        logger::info("Post-processing shader hook installed");

        RegisterPublishCallback(&Reapply);
    }
}
