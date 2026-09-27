#include "PostProcessing.h"

#include "Config.h"
#include "PresentHook.h"
#include "RenderPass.h"
#include "VTablePatch.h"

#include "Features/DLSS.h"
#include "Features/LUT.h"
#include "Features/Upscaling.h"
#include "RE/BSGraphics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <d3d11.h>
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
        // Mirrors SimpleGraphicsSuiteSettings in Settings.hlsli.
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
            float filmGrain;
            float grainTime;
            float lensFlare;
            float distanceHaze;
            float cameraNear;
            float cameraFar;
            float highlightGlow;
            float tonemapExposureOffset;
            float contactShadows;
            float lightDirX;
            float lightDirY;
            float lightDirZ;
            float loadingScreen;
            float underwaterWarp;
            float lensDrops;
            float lensDropsTime;
            float diveSplash;
            float lensFilm;
            float pad1;
            float pad2;
        };
        static_assert(sizeof(SettingsCB) == 128);

        // Matches SGS_CSLightData in ContactShadows.hlsli.
        constexpr std::size_t kMaxContactLights = 4;
        struct ContactLightsCB
        {
            float lights[kMaxContactLights][4];  // xyz world pos, w range
            float count;
            float interior;
            float pad0;
            float pad1;
        };
        static_assert(sizeof(ContactLightsCB) == kMaxContactLights * 16 + 16);

        bool NeedsReplacedShader(const Settings& a_settings);  // defined near ApplyEnabled below
        void ApplyEnabled(bool a_enabled);

        std::atomic<bool> g_reapplyPending{ false };
        std::atomic<bool> g_reloadPending{ false };
        void              ReloadShaders();

        bool IsPlayerSneaking()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && player->IsSneaking();
        }

        RE::NiPoint3 SunDirectionWorld()
        {
            auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
            if (!ssn)
                return {};
            auto* sunLight = ssn->GetRuntimeData().sunLight;
            if (!sunLight)
                return {};
            auto* dirLight = skyrim_cast<RE::NiDirectionalLight*>(sunLight->light.get());
            if (!dirLight)
                return {};
            const auto& worldDir = dirLight->GetWorldDirection();
            return { -worldDir.x, -worldDir.y, -worldDir.z };
        }

        bool g_motionBlurSuppressed = false;
        // Motion vectors are only written while the engine's TAA flag is on.
        bool g_motionVectorsValid = false;
        bool g_loadingScreenApplied = false;

        // Cached once per frame from OnPrePresent.
        bool g_pausedByMenu = false;
        bool g_loadingMenuOpen = false;

        void OnPrePresent(REX::W32::ID3D11Device*, REX::W32::ID3D11DeviceContext*, REX::W32::IDXGISwapChain*)
        {
            auto* ui = RE::UI::GetSingleton();
            g_pausedByMenu = ui && ui->GameIsPaused();
            g_loadingMenuOpen = ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
        }

        constexpr float                      kVignetteFadeSeconds = 0.35f;
        float                                 g_vignetteCurrent = 0.0f;
        std::chrono::steady_clock::time_point g_vignetteLastTick = std::chrono::steady_clock::now();

        const std::chrono::steady_clock::time_point kGrainStart = std::chrono::steady_clock::now();

        float GrainTime()
        {
            const auto seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - kGrainStart).count();
            return std::fmod(seconds, 1000.0f);
        }

        // Returns true while still transitioning.
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

        // The engine's flag can stay stuck after surfacing, hence the height check.
        bool IsCameraUnderwater(const RE::NiPoint3& a_camera)
        {
            auto* water = RE::TESWaterSystem::GetSingleton();
            return water && water->playerUnderwater && a_camera.z < water->underwaterHeight;
        }

        // Sheltered when anything with collision is straight above the camera.
        bool IsRainOnCamera(const RE::NiPoint3& a_camera)
        {
            auto* sky = RE::Sky::GetSingleton();
            auto* tes = RE::TES::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!sky || !tes || !player || sky->mode.get() != RE::Sky::Mode::kFull || !sky->IsRaining())
                return false;

            constexpr float kRayHeight = 100000.0f;  // above any worldspace geometry
            const float     scale = RE::bhkWorld::GetWorldScale();

            RE::bhkPickData pick{};
            // The player's own filter, so the ray passes through the player.
            player->GetCollisionFilterInfo(pick.rayInput.filterInfo);
            pick.rayInput.from = RE::hkVector4(a_camera.x * scale, a_camera.y * scale, a_camera.z * scale, 0.0f);
            pick.rayInput.to = RE::hkVector4(a_camera.x * scale, a_camera.y * scale, (a_camera.z + kRayHeight) * scale, 0.0f);
            tes->Pick(pick);
            return !pick.rayOutput.HasHit();
        }

        constexpr float kUnderwaterFadeSeconds = 0.25f;
        constexpr float kDiveSplashSeconds = 1.0f;
        constexpr float kLensDropsSeconds = 4.0f;
        constexpr float kRainWetSeconds = 2.0f;
        constexpr float kRainLensDrops = 0.5f;
        // A menu that stops rendering must not skip the animation ahead.
        constexpr float kMaxWaterStepSeconds = 0.1f;

        float                                 g_underwaterCurrent = 0.0f;
        float                                 g_diveSplash = 0.0f;
        float                                 g_lensDropsCurrent = 0.0f;
        float                                 g_lensFilm = 0.0f;
        float                                 g_lensDropsTime = 0.0f;
        bool                                  g_wasUnderwater = false;
        bool                                  g_waterPrimed = false;
        bool                                  g_waterWasActive = false;
        std::chrono::steady_clock::time_point g_waterLastTick = std::chrono::steady_clock::now();

        void ResetWater()
        {
            g_underwaterCurrent = 0.0f;
            g_diveSplash = 0.0f;
            g_lensDropsCurrent = 0.0f;
            g_lensFilm = 0.0f;
            g_waterPrimed = false;
        }

        bool StepWater(const Settings& a_settings)
        {
            const auto now = std::chrono::steady_clock::now();
            const float delta = g_pausedByMenu ? 0.0f :
                std::clamp(std::chrono::duration<float>(now - g_waterLastTick).count(), 0.0f, kMaxWaterStepSeconds);
            g_waterLastTick = now;

            auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
            const auto& pp = a_settings.postProcessing;
            if (!a_settings.masterEnabled || (!pp.underwaterEffects && !pp.rainDrops) || g_loadingMenuOpen || !ssn) {
                ResetWater();
            } else {
                const auto camera = ssn->GetRuntimeData().cameraPos;
                const bool underwater = IsCameraUnderwater(camera);
                // After a load or a toggle, adopt the current state instead of treating it as a transition.
                if (!g_waterPrimed) {
                    g_wasUnderwater = underwater;
                    g_waterPrimed = true;
                }
                if (!pp.underwaterEffects) {
                    g_diveSplash = 0.0f;
                    g_lensFilm = 0.0f;
                } else if (!g_wasUnderwater && underwater) {
                    g_diveSplash = 1.0f;
                } else if (g_wasUnderwater && !underwater) {
                    g_diveSplash = 0.0f;
                    g_lensDropsCurrent = 1.0f;
                    g_lensFilm = 1.0f;
                    // Varying origin: a new drop layout each time.
                    g_lensDropsTime = GrainTime();
                }
                g_wasUnderwater = underwater;

                const float fade = delta / kUnderwaterFadeSeconds;
                g_underwaterCurrent = underwater && pp.underwaterEffects ? std::min(g_underwaterCurrent + fade, 1.0f) :
                                                                         std::max(g_underwaterCurrent - fade, 0.0f);

                if (underwater) {
                    g_diveSplash = std::max(g_diveSplash - delta / kDiveSplashSeconds, 0.0f);
                    g_lensDropsCurrent = 0.0f;
                    g_lensFilm = 0.0f;
                } else {
                    // Nothing moves while paused, so the ray can wait.
                    const float target = delta > 0.0f && pp.rainDrops && IsRainOnCamera(camera) ? kRainLensDrops : 0.0f;
                    if (g_lensDropsCurrent < target) {
                        if (g_lensDropsCurrent <= 0.0f)
                            g_lensDropsTime = GrainTime();
                        g_lensDropsCurrent = std::min(g_lensDropsCurrent + delta / kRainWetSeconds, target);
                    } else {
                        g_lensDropsCurrent = std::max(g_lensDropsCurrent - delta / kLensDropsSeconds, target);
                    }
                    g_lensFilm = std::max(g_lensFilm - delta / kLensDropsSeconds, 0.0f);
                    if (g_lensDropsCurrent > 0.0f)
                        g_lensDropsTime += delta;
                }
            }

            const bool active = g_underwaterCurrent > 0.0f || g_diveSplash > 0.0f || g_lensDropsCurrent > 0.0f;
            const bool dirty = active || g_waterWasActive;
            g_waterWasActive = active;
            return dirty;
        }

        REX::W32::ID3D11Buffer* g_settingsBuffer = nullptr;
        REX::W32::ID3D11Buffer*             g_contactLightsBuffer = nullptr;
        REX::W32::ID3D11ShaderResourceView* g_contactLightsSRV = nullptr;

        REX::W32::ID3D11Buffer* CreateDynamicCB(std::uint32_t a_byteWidth)
        {
            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            REX::W32::D3D11_BUFFER_DESC desc{};
            desc.byteWidth = a_byteWidth;
            desc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
            desc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
            desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;

            REX::W32::ID3D11Buffer* buffer = nullptr;
            if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)))
                logger::error("Post-processing: couldn't create a constant buffer");
            return buffer;
        }

        void EnsureSettingsBuffer()
        {
            if (!g_settingsBuffer)
                g_settingsBuffer = CreateDynamicCB(sizeof(SettingsCB));
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
            dst->motionBlurAmount = g_motionBlurSuppressed ? 0.0f : postProcessing.motionBlurStrength;
            dst->upscalingEnabled = Upscaling::IsActive(a_settings) ? 1.0f : 0.0f;
            dst->lutStrength = LUT::CurrentSRV() ? postProcessing.lutStrength : 0.0f;
            dst->lutSize = static_cast<float>(LUT::CurrentSize());
            dst->tonemapMethod = static_cast<float>(postProcessing.tonemapMethod);
            dst->tonemapExposureOffset = postProcessing.tonemapExposureOffset;
            dst->vignette = postProcessing.vignetteSneakOnly ? g_vignetteCurrent : postProcessing.vignette;
            dst->postProcessingEnabled = postProcessing.enabled ? 1.0f : 0.0f;
            dst->filmGrain = postProcessing.filmGrain;
            dst->grainTime = GrainTime();
            dst->lensFlare = postProcessing.lensFlare;
            dst->distanceHaze = postProcessing.distanceHaze;
            dst->cameraNear = RE::BSGraphics::CameraNear();
            dst->cameraFar = RE::BSGraphics::CameraFar();
            dst->highlightGlow = postProcessing.highlightGlow;
            constexpr float kContactShadowStrength = 0.3f;
            dst->contactShadows = postProcessing.contactShadows ? kContactShadowStrength : 0.0f;

            const auto sunDir = postProcessing.contactShadows ? SunDirectionWorld() : RE::NiPoint3{};
            dst->lightDirX = sunDir.x;
            dst->lightDirY = sunDir.y;
            dst->lightDirZ = sunDir.z;
            dst->loadingScreen = g_loadingMenuOpen ? 1.0f : 0.0f;
            dst->underwaterWarp = g_underwaterCurrent;
            dst->lensDrops = g_lensDropsCurrent;
            dst->lensDropsTime = g_lensDropsTime;
            dst->diveSplash = g_diveSplash;
            dst->lensFilm = g_lensFilm;

            context->Unmap(static_cast<REX::W32::ID3D11Resource*>(g_settingsBuffer), 0);
        }

        // Nearest point lights to the camera, for indoor contact shadows.
        void GatherContactLights(ContactLightsCB& a_data)
        {
            auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
            if (!ssn)
                return;

            const auto& runtimeData = ssn->GetRuntimeData();
            const auto  cameraPos = runtimeData.cameraPos;

            struct Candidate
            {
                float        distance;
                RE::NiPoint3 position;
                float        range;
            };
            std::array<Candidate, kMaxContactLights> nearest{};
            std::size_t                              found = 0;

            for (const auto& light : runtimeData.activeLights) {
                auto* bsLight = light.get();
                if (!bsLight || !bsLight->pointLight || !bsLight->light)
                    continue;

                const float range = bsLight->light->GetLightRuntimeData().radius.x;
                if (range <= 0.0f)
                    continue;

                const Candidate candidate{ cameraPos.GetDistance(bsLight->worldTranslate), bsLight->worldTranslate,
                    range };

                std::size_t slot = std::min(found, kMaxContactLights - 1);
                if (found < kMaxContactLights)
                    ++found;
                else if (candidate.distance >= nearest[slot].distance)
                    continue;

                while (slot > 0 && candidate.distance < nearest[slot - 1].distance) {
                    nearest[slot] = nearest[slot - 1];
                    --slot;
                }
                nearest[slot] = candidate;
            }

            a_data.count = static_cast<float>(found);
            for (std::size_t i = 0; i < found; ++i) {
                a_data.lights[i][0] = nearest[i].position.x;
                a_data.lights[i][1] = nearest[i].position.y;
                a_data.lights[i][2] = nearest[i].position.z;
                a_data.lights[i][3] = nearest[i].range;
            }
        }

        void EnsureContactLightsBuffer()
        {
            if (g_contactLightsBuffer)
                return;

            auto* device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;

            REX::W32::D3D11_BUFFER_DESC desc{};
            desc.byteWidth = sizeof(ContactLightsCB);
            desc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
            desc.bindFlags = REX::W32::D3D11_BIND_SHADER_RESOURCE;
            desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;

            if (FAILED(device->CreateBuffer(&desc, nullptr, &g_contactLightsBuffer))) {
                logger::error("Post-processing: couldn't create the contact-light buffer");
                return;
            }

            REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.format = REX::W32::DXGI_FORMAT_R32G32B32A32_FLOAT;
            srv.viewDimension = REX::W32::D3D_SRV_DIMENSION_BUFFER;
            srv.buffer.firstElement = 0;
            srv.buffer.numElements = sizeof(ContactLightsCB) / 16;
            if (FAILED(device->CreateShaderResourceView(
                    static_cast<REX::W32::ID3D11Resource*>(g_contactLightsBuffer), &srv, &g_contactLightsSRV)))
                logger::error("Post-processing: couldn't create the contact-light SRV");
        }

        void UpdateContactLightsBuffer()
        {
            EnsureContactLightsBuffer();
            if (!g_contactLightsBuffer)
                return;

            ContactLightsCB data{};
            auto*           sky = RE::Sky::GetSingleton();
            const bool      interior = sky && sky->mode.get() == RE::Sky::Mode::kInterior;
            data.interior = interior ? 1.0f : 0.0f;
            if (interior)
                GatherContactLights(data);

            auto* context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;
            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(static_cast<REX::W32::ID3D11Resource*>(g_contactLightsBuffer), 0,
                    REX::W32::D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                return;
            *static_cast<ContactLightsCB*>(mapped.data) = data;
            context->Unmap(static_cast<REX::W32::ID3D11Resource*>(g_contactLightsBuffer), 0);
        }

        REX::W32::ID3D11PixelShader* CompilePixelShader(const std::filesystem::path& a_path)
        {
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

            return RenderPass::CompilePixelShader(a_path, macros);
        }

        // Files are named <technique ID in hex>.ps.hlsl, as in Vanilla HDR.
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

        constexpr std::array kTonemapShaderNames = { "ISHDRTonemapBlendCinematic"sv, "ISHDRTonemapBlendCinematicFade"sv };

        using SetupTechnique_t = bool (*)(RE::BSShader*, std::uint32_t);
        std::unordered_map<void*, SetupTechnique_t> g_patchedVtables;

        // Before the vanilla chain, which reads kMAIN and needs the de-jittered result.
        bool ApplyDLSS()
        {
            const auto settings = ActiveSettings();
            if (!settings->masterEnabled || !settings->antiAliasing.enabled || settings->antiAliasing.method != 2 ||
                g_loadingMenuOpen)
                return false;

            auto& main = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
            if (!main.texture) {
                DLSS::SetLastFailureReason("kMAIN render target has no texture yet");
                return false;
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
            return applied;
        }

        // Runs on every technique draw, keep it cheap.
        bool Hook_SetupTechnique(RE::BSShader* a_this, std::uint32_t a_technique)
        {
            if (g_reloadPending.exchange(false))
                ReloadShaders();
            if (g_reapplyPending.exchange(false)) {
                const auto settings = ActiveSettings();
                UpdateSettingsBuffer(*settings);
                ApplyEnabled(NeedsReplacedShader(*settings));
            }

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
                // ActiveSettings() locks, take one snapshot.
                const auto  settingsPtr = ActiveSettings();
                const auto& settings = *settingsPtr;

                if (g_loadingMenuOpen != g_loadingScreenApplied) {
                    g_loadingScreenApplied = g_loadingMenuOpen;
                    UpdateSettingsBuffer(settings);
                }

                if (auto* lutSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(LUT::CurrentSRV())) {
                    runtimeData.context->PSSetShaderResources(17, 1, &lutSRV);
                }

                // Not while a menu pauses the game, the map and wait menu would smear.
                {
                    const bool suppressed = g_pausedByMenu || !g_motionVectorsValid;
                    if (suppressed != g_motionBlurSuppressed) {
                        g_motionBlurSuppressed = suppressed;
                        UpdateSettingsBuffer(settings);
                    }

                    if (settings.masterEnabled && settings.postProcessing.motionBlurStrength > 0.0f && !suppressed) {
                        auto* motionSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(
                            runtimeData.renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV);
                        runtimeData.context->PSSetShaderResources(16, 1, &motionSRV);
                    }

                    if (settings.masterEnabled &&
                        (settings.postProcessing.motionBlurStrength > 0.0f || settings.postProcessing.distanceHaze > 0.0f ||
                            settings.postProcessing.contactShadows)) {
                        auto& depthStencils = RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData();
                        auto* depthSRV = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(
                            depthStencils.depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN].depthSRV);
                        runtimeData.context->PSSetShaderResources(18, 1, &depthSRV);
                    }
                }

                if (settings.masterEnabled && settings.postProcessing.contactShadows) {
                    UpdateContactLightsBuffer();
                    if (g_contactLightsSRV)
                        runtimeData.context->PSSetShaderResources(19, 1, &g_contactLightsSRV);
                }

                if (settings.postProcessing.vignetteSneakOnly) {
                    const float target = IsPlayerSneaking() ? settings.postProcessing.vignette : 0.0f;
                    if (StepVignette(target))
                        UpdateSettingsBuffer(settings);
                } else {
                    g_vignetteCurrent = settings.postProcessing.vignette;
                }

                const bool waterDirty = StepWater(settings);
                if (settings.postProcessing.filmGrain > 0.0f || settings.postProcessing.contactShadows || waterDirty)
                    UpdateSettingsBuffer(settings);
            }

            return result;
        }

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

            // Every replaced shader reads the settings cbuffer.
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

        using MainPostProcessing_t = void (*)(RE::ImageSpaceManager*, std::uint32_t, RE::RENDER_TARGET, void*, bool);
        MainPostProcessing_t g_originalMainPostProcessing = nullptr;

        void Hook_MainPostProcessing(
            RE::ImageSpaceManager* a_this, std::uint32_t a3, RE::RENDER_TARGET a_target, void* a4, bool a5)
        {
            // DLAA already resolved kMAIN, skip the vanilla TAA pass in this chain.
            auto* taa = RE::BSGraphics::TAAState::GetSingleton();
            g_motionVectorsValid = taa && taa->IsTAAEnabled();
            if (!ApplyDLSS() || !taa || !taa->inner) {
                g_originalMainPostProcessing(a_this, a3, a_target, a4, a5);
                return;
            }

            const bool taaEnabled = taa->inner->taaEnabled;
            taa->inner->taaEnabled = false;
            g_originalMainPostProcessing(a_this, a3, a_target, a4, a5);
            taa->inner->taaEnabled = taaEnabled;
        }

        constexpr std::uintptr_t kLoadShadersEntrySize = 9;

        struct Patch : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume, std::uintptr_t a_nullStream)
            {
                Xbyak::Label nullStream;

                test(rdx, rdx);
                jz(nullStream);
                jmp(ptr[rip]);
                dq(a_resume);

                L(nullStream);
                jmp(ptr[rip]);
                dq(a_nullStream);
            }
        };

        std::uintptr_t NullStreamTarget(std::uintptr_t a_entry)
        {
            constexpr std::array<std::uint8_t, 5> kEntry = { 0x48, 0x85, 0xD2, 0x0F, 0x84 };
            if (std::memcmp(reinterpret_cast<const void*>(a_entry), kEntry.data(), kEntry.size()) != 0)
                return 0;

            std::int32_t displacement;
            std::memcpy(&displacement, reinterpret_cast<const void*>(a_entry + kEntry.size()), sizeof(displacement));
            return a_entry + kLoadShadersEntrySize + displacement;
        }

        // FSR1 EASU lives in the replaced shader too.
        bool NeedsReplacedShader(const Settings& a_settings)
        {
            if (!a_settings.masterEnabled)
                return false;

            const auto& pp = a_settings.postProcessing;
            return pp.enabled || pp.sharpening > 0.0f || pp.motionBlurStrength > 0.0f || pp.vignette > 0.0f ||
                pp.filmGrain > 0.0f || pp.lensFlare > 0.0f || pp.distanceHaze > 0.0f || pp.highlightGlow > 0.0f ||
                pp.underwaterEffects || pp.rainDrops || pp.contactShadows || !pp.lutName.empty() || a_settings.upscaling.enabled;
        }

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

        void ReloadShaders()
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
    }

    void Reapply() { g_reapplyPending.store(true); }

    std::size_t ReplacedShaderCount() { return g_replaced.size(); }

    bool IsInstalled() { return !g_patchedVtables.empty(); }

    // Applied on the next draw.
    void ReloadShadersFromDisk() { g_reloadPending.store(true); }

    void InstallHooks()
    {
        auto& trampoline = SKSE::GetTrampoline();

        const auto target = RELOCATION_ID(101339, 108326).address();
        if (const auto nullStream = NullStreamTarget(target)) {
            Patch patch{ target + kLoadShadersEntrySize, nullStream };
            patch.ready();
            g_originalLoadShaders = reinterpret_cast<LoadShaders_t>(trampoline.allocate(patch));
            trampoline.write_branch<6>(target, Hook_LoadShaders);
            logger::info("Post-processing shader hook installed");
        } else {
            logger::error("Post-processing: unexpected BSShader::LoadShaders entry, shaders won't be replaced");
        }

        // Checked against open-shaders' Main_PostProcessing, VR included.
        const auto postProcessingCall =
            RELOCATION_ID(100430, 107148).address() + REL::Relocate<std::uintptr_t>(0x1F0, 0x1E7, 0x206);
        g_originalMainPostProcessing =
            reinterpret_cast<MainPostProcessing_t>(trampoline.write_call<5>(postProcessingCall, &Hook_MainPostProcessing));

        RegisterPublishCallback(&Reapply);
        PresentHook::RegisterPrePresent(&OnPrePresent);
    }
}
