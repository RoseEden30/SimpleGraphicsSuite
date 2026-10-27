#pragma once

// Undocumented engine internals. The TAA and sampler state come from doodlum's
// skyrim-lod-bias, adapted to CommonLibSSE-NG's REX::W32 D3D11 wrappers.

#include <cmath>
#include <optional>

namespace RE::BSGraphics
{
    // The game's "TAA is active" flag. It sits further into ImageSpaceManager
    // in VR, so it goes through CommonLib's per-runtime layouts.
    inline bool* TAAEnabledFlag()
    {
        auto* manager = ImageSpaceManager::GetSingleton();
        if (!manager)
            return nullptr;

        auto* temporalAA = REL::Module::IsVR() ? manager->GetVRRuntimeData()->BSImagespaceShaderISTemporalAA :
                                                 manager->GetRuntimeData().BSImagespaceShaderISTemporalAA;
        return temporalAA ? &temporalAA->taaEnabled : nullptr;
    }

    // The game's array of sampler states, indexed by address mode and filter
    // mode. Index [addressMode][3] is the linear filter, the only one this
    // module touches.
    struct SamplerStates
    {
        static constexpr std::size_t kAddressModes = 6;
        static constexpr std::size_t kFilterModes = 5;
        static constexpr std::size_t kLinearFilter = 3;

        REX::W32::ID3D11SamplerState* states[kAddressModes][kFilterModes];

        static SamplerStates* GetSingleton()
        {
            REL::Relocation<SamplerStates*> instance{ RELOCATION_ID(524751, 411366) };
            return instance.get();
        }
    };

    inline float CameraNear() { return *REL::Relocation<float*>(RELOCATION_ID(517032, 403540), 0x40); }
    inline float CameraFar() { return *REL::Relocation<float*>(RELOCATION_ID(517032, 403540), 0x44); }

    // SE/AE layout of a State::cameraDataCacheA entry, compiled out of CommonLib's multi-runtime build.
#pragma warning(push)
#pragma warning(disable : 4324)
    struct CameraStateEntry
    {
        NiCamera* camera;             // 000
        ViewData  view;               // 010
        NiPoint3  posAdjust;          // 260
        NiPoint3  currentPosAdjust;   // 26C
        NiPoint3  previousPosAdjust;  // 278
        bool      useJitter;          // 284
    };
#pragma warning(pop)
    static_assert(offsetof(CameraStateEntry, view) == 0x10);
    static_assert(offsetof(CameraStateEntry, posAdjust) == 0x260);
    static_assert(offsetof(CameraStateEntry, useJitter) == 0x284);
    static_assert(sizeof(CameraStateEntry) == 0x290);

    // What the world camera was drawn with this frame. Unlike the PerFrame
    // buffer, a menu's camera can't overwrite it.
    inline const CameraStateEntry* FindWorldCameraState(bool a_jitter)
    {
        auto* camera = Main::WorldRootCamera();
        auto* state = State::GetSingleton();
        if (!camera || !state)
            return nullptr;

        const auto&             cache = state->GetRuntimeData().cameraDataCacheA;
        const auto*             entries = reinterpret_cast<const CameraStateEntry*>(cache.data());
        const CameraStateEntry* match = nullptr;
        for (std::uint32_t i = 0; i < cache.size(); ++i) {
            if (entries[i].camera != camera)
                continue;
            if (entries[i].useJitter == a_jitter)
                return &entries[i];
            match = &entries[i];
        }
        return match;
    }

    // Rows as outputs (clip = M * v), however the engine stored them.
    struct CameraMatrices
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix projUnjittered;
        DirectX::SimpleMath::Matrix viewProjUnjittered;
        DirectX::SimpleMath::Matrix previousViewProjUnjittered;
    };

    // The storage order comes from which element holds the projection's w, as
    // in Open Shaders' Reverse-Z. Empty when it isn't a perspective projection.
    inline std::optional<CameraMatrices> GetCameraMatrices(const ViewData& a_view)
    {
        const auto& proj = a_view.projMat;
        const bool  wFromRow3 = std::abs(std::abs(proj.m[3][2]) - 1.0f) < 1e-3f;
        const bool  wFromColumn3 = std::abs(std::abs(proj.m[2][3]) - 1.0f) < 1e-3f;
        if (std::abs(proj.m[3][3]) > 1e-4f || (!wFromRow3 && !wFromColumn3))
            return std::nullopt;

        const auto order = [&](const DirectX::SimpleMath::Matrix& a_matrix) {
            return wFromColumn3 ? a_matrix.Transpose() : a_matrix;
        };
        return CameraMatrices{ order(a_view.viewMat), order(a_view.projMatrixUnjittered),
            order(a_view.viewProjMatrixUnjittered), order(a_view.previousViewProjMatrixUnjittered) };
    }
}
