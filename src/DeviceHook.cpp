#include "DeviceHook.h"

#include "Streamline.h"

#include <d3d11.h>

namespace DeviceHook
{
    namespace
    {
        using D3D11CreateDeviceAndSwapChain_t = decltype(&REX::W32::D3D11CreateDeviceAndSwapChain);
        D3D11CreateDeviceAndSwapChain_t g_original = nullptr;

        REX::W32::HRESULT __stdcall Hook_D3D11CreateDeviceAndSwapChain(
            REX::W32::IDXGIAdapter* a_adapter, REX::W32::D3D_DRIVER_TYPE a_driverType, REX::W32::HMODULE a_software,
            std::uint32_t a_flags, const REX::W32::D3D_FEATURE_LEVEL* a_featureLevels,
            std::uint32_t a_featureLevelCount, std::uint32_t a_sdkVersion,
            const REX::W32::DXGI_SWAP_CHAIN_DESC* a_swapChainDesc, REX::W32::IDXGISwapChain** a_swapChain,
            REX::W32::ID3D11Device** a_device, REX::W32::D3D_FEATURE_LEVEL* a_featureLevel,
            REX::W32::ID3D11DeviceContext** a_context)
        {
            const auto result = g_original(a_adapter, a_driverType, a_software, a_flags, a_featureLevels,
                a_featureLevelCount, a_sdkVersion, a_swapChainDesc, a_swapChain, a_device, a_featureLevel, a_context);

            if (SUCCEEDED(result)) {
                logger::info("DeviceHook: D3D11 device and swap chain created");

                // Manual-hooking mode needs both interfaces swapped for
                // Streamline's proxies before anyone touches them, or its
                // per-frame bookkeeping never runs. slSetD3DDevice then takes
                // the upgraded device.
                Streamline::EnsureInitialized();
                Streamline::UpgradeInterface(reinterpret_cast<void**>(a_device));
                Streamline::UpgradeInterface(reinterpret_cast<void**>(a_swapChain));
                Streamline::SetDevice(reinterpret_cast<ID3D11Device*>(*a_device));
            }

            return result;
        }
    }

    void Install()
    {
        const auto original =
            SKSE::PatchIAT(&Hook_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
        if (!original) {
            logger::error("DeviceHook: couldn't patch the D3D11CreateDeviceAndSwapChain import");
            return;
        }
        g_original = reinterpret_cast<D3D11CreateDeviceAndSwapChain_t>(original);

        logger::info("DeviceHook installed");
    }
}
