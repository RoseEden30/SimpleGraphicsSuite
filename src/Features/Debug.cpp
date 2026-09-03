#include "Debug.h"

#include "RE/BSGraphics.h"

namespace Debug
{
    namespace
    {
        // GPU adapter behind the device the renderer actually uses - not
        // necessarily the Windows default, useful when the dev box has more
        // than one GPU.
        std::string GetGPUDescription(std::uint32_t* a_outVendorId)
        {
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            auto* device = renderer ? renderer->GetRuntimeData().forwarder : nullptr;
            if (!device)
                return "unknown (no device yet)";

            REX::W32::IDXGIDevice* dxgiDevice = nullptr;
            if (FAILED(device->QueryInterface(REX::W32::IID_IDXGIDevice, reinterpret_cast<void**>(&dxgiDevice))) ||
                !dxgiDevice)
                return "unknown";

            REX::W32::IDXGIAdapter* adapter = nullptr;
            const auto hadAdapter = SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter;
            dxgiDevice->Release();
            if (!hadAdapter)
                return "unknown";

            REX::W32::DXGI_ADAPTER_DESC desc{};
            const auto hadDesc = SUCCEEDED(adapter->GetDesc(&desc));
            adapter->Release();
            if (!hadDesc)
                return "unknown";

            if (a_outVendorId)
                *a_outVendorId = desc.vendorId;

            const int len =
                WideCharToMultiByte(CP_UTF8, 0, desc.description, -1, nullptr, 0, nullptr, nullptr);
            if (len <= 1)
                return "unknown";
            std::string result(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, desc.description, -1, result.data(), len, nullptr, nullptr);
            return result;
        }

        const char* VendorName(std::uint32_t a_vendorId)
        {
            switch (a_vendorId) {
            case 0x10DE:
                return "NVIDIA";
            case 0x1002:
                return "AMD";
            case 0x8086:
                return "Intel";
            default:
                return "unknown";
            }
        }
    }

    std::string GpuSummary()
    {
        std::uint32_t     vendorId = 0;
        const std::string description = GetGPUDescription(&vendorId);
        return description + " (" + VendorName(vendorId) + ")";
    }
}
