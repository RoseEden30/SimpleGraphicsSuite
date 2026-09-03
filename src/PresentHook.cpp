#include "PresentHook.h"

#include "VTablePatch.h"

#include <vector>

namespace PresentHook
{
    namespace
    {
        REX::W32::ID3D11Device*        g_device = nullptr;
        REX::W32::ID3D11DeviceContext* g_context = nullptr;
        REX::W32::IDXGISwapChain*      g_swapChain = nullptr;

        using Present_t = REX::W32::HRESULT(REX::W32::IDXGISwapChain*, std::uint32_t, std::uint32_t);
        Present_t* g_originalPresent = nullptr;

        std::vector<Callback> g_prePresent;
        std::vector<Callback> g_postPresent;

        REX::W32::HRESULT __stdcall Hook_Present(
            REX::W32::IDXGISwapChain* a_self, std::uint32_t a_syncInterval, std::uint32_t a_flags)
        {
            for (auto* callback : g_prePresent)
                callback(g_device, g_context, a_self);

            const auto result = g_originalPresent(a_self, a_syncInterval, a_flags);

            for (auto* callback : g_postPresent)
                callback(g_device, g_context, a_self);

            return result;
        }
    }

    bool Install()
    {
        if (g_originalPresent)
            return true;

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        auto& runtimeData = renderer->GetRuntimeData();

        g_device = runtimeData.forwarder;
        g_context = runtimeData.context;
        g_swapChain = runtimeData.renderWindows[0].swapChain;

        if (!g_device || !g_context || !g_swapChain) {
            logger::error("PresentHook: no D3D11 device, context or swapchain");
            return false;
        }

        if (!VTablePatch::PatchSlot(g_swapChain, 8, reinterpret_cast<void*>(&Hook_Present),
                       reinterpret_cast<void**>(&g_originalPresent))) {
            logger::error("PresentHook: couldn't hook IDXGISwapChain::Present");
            return false;
        }

        logger::info("PresentHook installed");
        return true;
    }

    void RegisterPrePresent(Callback a_callback) { g_prePresent.push_back(a_callback); }
    void RegisterPostPresent(Callback a_callback) { g_postPresent.push_back(a_callback); }
}
