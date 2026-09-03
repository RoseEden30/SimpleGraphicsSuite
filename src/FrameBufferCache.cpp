#include "FrameBufferCache.h"

#include "RE/BSGraphics.h"
#include "VTablePatch.h"

#include <d3d11.h>

// Relocation for the engine's own per-frame camera constant buffer object -
// verified against Community Shaders' own working Globals.cpp, not guessed.
namespace
{
    REL::Relocation<ID3D11Buffer**> g_perFrameBuffer{ RELOCATION_ID(524768, 411384) };

    D3D11_MAPPED_SUBRESOURCE* g_mapped = nullptr;
    FrameBufferCache::Data    g_data{};

    using Map_t = HRESULT(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
    using Unmap_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);

    Map_t   g_originalMap = nullptr;
    Unmap_t g_originalUnmap = nullptr;

    HRESULT STDMETHODCALLTYPE Hook_Map(ID3D11DeviceContext* a_this, ID3D11Resource* a_resource,
        UINT a_subresource, D3D11_MAP a_mapType, UINT a_mapFlags, D3D11_MAPPED_SUBRESOURCE* a_mapped)
    {
        const auto result = g_originalMap(a_this, a_resource, a_subresource, a_mapType, a_mapFlags, a_mapped);
        if (SUCCEEDED(result) && a_resource == reinterpret_cast<ID3D11Resource*>(*g_perFrameBuffer))
            g_mapped = a_mapped;
        return result;
    }

    void STDMETHODCALLTYPE Hook_Unmap(ID3D11DeviceContext* a_this, ID3D11Resource* a_resource, UINT a_subresource)
    {
        if (a_resource == reinterpret_cast<ID3D11Resource*>(*g_perFrameBuffer) && g_mapped) {
            g_data = *static_cast<const FrameBufferCache::Data*>(g_mapped->pData);
            g_mapped = nullptr;
        }
        g_originalUnmap(a_this, a_resource, a_subresource);
    }
}

namespace FrameBufferCache
{
    void InstallHooks()
    {
        auto* context = reinterpret_cast<ID3D11DeviceContext*>(
            RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context);
        if (!context) {
            logger::warn("FrameBufferCache: no device context yet, couldn't install hooks");
            return;
        }

        void* originalMap = nullptr;
        void* originalUnmap = nullptr;
        // Map = slot 14, Unmap = slot 15 on ID3D11DeviceContext - fixed,
        // public Microsoft COM layout (IUnknown 0-2, ID3D11DeviceChild 3-6,
        // then ID3D11DeviceContext's own methods from 7), not engine-specific.
        const bool mapOk = VTablePatch::PatchSlot(context, 14, reinterpret_cast<void*>(&Hook_Map), &originalMap);
        const bool unmapOk = VTablePatch::PatchSlot(context, 15, reinterpret_cast<void*>(&Hook_Unmap), &originalUnmap);
        if (!mapOk || !unmapOk) {
            // Leaving one slot hooked with no original to call through is a
            // guaranteed null-function-pointer crash on the next Map/Unmap -
            // undo whichever half succeeded.
            void* discard = nullptr;
            if (mapOk)
                VTablePatch::PatchSlot(context, 14, originalMap, &discard);
            if (unmapOk)
                VTablePatch::PatchSlot(context, 15, originalUnmap, &discard);
            logger::warn("FrameBufferCache: couldn't hook ID3D11DeviceContext::Map/Unmap");
            return;
        }

        g_originalMap = reinterpret_cast<Map_t>(originalMap);
        g_originalUnmap = reinterpret_cast<Unmap_t>(originalUnmap);
        logger::info("FrameBufferCache: hooks installed");
    }

    const Data& Get() { return g_data; }
}
