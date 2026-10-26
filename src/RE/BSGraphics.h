#pragma once

// Undocumented engine internals needed by the anti-aliasing module. Offsets
// and relocation IDs come from doodlum's skyrim-lod-bias, adapted to
// CommonLibSSE-NG's REX::W32 D3D11 wrappers.

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
}
