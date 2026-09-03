#pragma once

// Undocumented engine internals needed by the anti-aliasing module. Offsets
// and relocation IDs come from doodlum's skyrim-lod-bias, adapted to
// CommonLibSSE-NG's REX::W32 D3D11 wrappers.

namespace RE::BSGraphics
{
    // Holds the game's "TAA is active" flag. Only the flag is used here, so
    // everything ahead of it is untyped padding.
    struct TAAState
    {
        struct Inner
        {
            std::uint8_t unk00[0x18];
            bool taaEnabled;
        };

        std::uint8_t unk00[0x1F0];
        Inner*       inner;

        static TAAState* GetSingleton()
        {
            REL::Relocation<TAAState**> instance{ RELOCATION_ID(527731, 414660) };  // 31D11A0, 326B280
            return *instance;
        }

        bool IsTAAEnabled() const { return inner && inner->taaEnabled; }
    };

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
}
