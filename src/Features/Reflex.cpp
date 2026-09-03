#include "Reflex.h"

#include "Config.h"
#include "PresentHook.h"

#pragma warning(push)
#pragma warning(disable : 4828)
#include <nvapi.h>
#pragma warning(pop)

#include <atomic>

namespace Reflex
{
    namespace
    {
        bool g_supported = false;

        REX::W32::ID3D11Device* g_device = nullptr;

        // Read from the per-frame marker hooks, written from the publish
        // callback - so nothing NvAPI-related runs while Reflex is off.
        std::atomic<bool> g_active{ false };

        // What was last handed to the driver, to skip identical calls during
        // a drag and to log only real changes.
        NV_SET_SLEEP_MODE_PARAMS g_appliedParams{};
        bool                     g_everApplied = false;

        void ApplySleepMode(const Settings& a_settings)
        {
            if (!g_supported || !g_device)
                return;

            // The global bypass has to reach the driver, not just skip the
            // per-frame Sleep() call.
            const bool  active = a_settings.masterEnabled && a_settings.reflex.enabled;
            const auto& config = a_settings.reflex;

            NV_SET_SLEEP_MODE_PARAMS params{};
            params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
            params.bLowLatencyMode = active && config.lowLatencyMode;
            params.bLowLatencyBoost = active && config.lowLatencyBoost;
            params.bUseMarkersToOptimize = active && config.useMarkersToOptimize;
            params.minimumIntervalUs = active && config.useFpsLimit && config.fpsLimit > 0.0f
                                           ? static_cast<NvU32>(1'000'000.0f / config.fpsLimit)
                                           : 0;

            g_active.store(active, std::memory_order_relaxed);

            if (g_everApplied && params.bLowLatencyMode == g_appliedParams.bLowLatencyMode &&
                params.bLowLatencyBoost == g_appliedParams.bLowLatencyBoost &&
                params.bUseMarkersToOptimize == g_appliedParams.bUseMarkersToOptimize &&
                params.minimumIntervalUs == g_appliedParams.minimumIntervalUs)
                return;

            const auto status = NvAPI_D3D_SetSleepMode(reinterpret_cast<IUnknown*>(g_device), &params);
            if (status != NVAPI_OK) {
                logger::warn("NvAPI_D3D_SetSleepMode failed ({})", static_cast<int>(status));
                return;
            }

            g_appliedParams = params;
            g_everApplied = true;
            logger::info("Reflex: active={} lowLatency={} boost={} fpsLimitUs={}", active, params.bLowLatencyMode != 0,
                params.bLowLatencyBoost != 0, params.minimumIntervalUs);
        }

        void OnPublish() { ApplySleepMode(*ActiveSettings()); }

        void SetLatencyMarker(NV_LATENCY_MARKER_TYPE a_marker)
        {
            NV_LATENCY_MARKER_PARAMS params{};
            params.version = NV_LATENCY_MARKER_PARAMS_VER;
            params.frameID = 0;  // engine doesn't expose a frame counter here; markers still pace Sleep.
            params.markerType = a_marker;

            NvAPI_D3D_SetLatencyMarker(reinterpret_cast<IUnknown*>(g_device), &params);
        }

        bool IsActive() { return g_active.load(std::memory_order_relaxed); }

        void OnPrePresent(REX::W32::ID3D11Device*, REX::W32::ID3D11DeviceContext*, REX::W32::IDXGISwapChain*)
        {
            if (IsActive())
                SetLatencyMarker(PRESENT_START);
        }

        void OnPostPresent(REX::W32::ID3D11Device*, REX::W32::ID3D11DeviceContext*, REX::W32::IDXGISwapChain*)
        {
            if (!IsActive())
                return;

            SetLatencyMarker(PRESENT_END);
            NvAPI_D3D_Sleep(reinterpret_cast<IUnknown*>(g_device));
        }

        struct Main_Update_Start
        {
            static void thunk(std::int64_t a_unk)
            {
                if (IsActive()) {
                    SetLatencyMarker(SIMULATION_START);
                    SetLatencyMarker(INPUT_SAMPLE);
                }
                func(a_unk);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // NVIDIA vendor ID, straight from the PCI-SIG database.
        constexpr std::uint32_t kNvidiaVendorId = 0x10DE;

        bool DetectNvidia(REX::W32::ID3D11Device* a_device)
        {
            REX::W32::IDXGIDevice* dxgiDevice = nullptr;
            if (FAILED(a_device->QueryInterface(REX::W32::IID_IDXGIDevice, reinterpret_cast<void**>(&dxgiDevice))) ||
                !dxgiDevice)
                return false;

            REX::W32::IDXGIAdapter* adapter = nullptr;
            const auto hadAdapter = SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter;
            dxgiDevice->Release();

            if (!hadAdapter)
                return false;

            REX::W32::DXGI_ADAPTER_DESC desc{};
            const auto hadDesc = SUCCEEDED(adapter->GetDesc(&desc));
            adapter->Release();

            return hadDesc && desc.vendorId == kNvidiaVendorId;
        }
    }

    bool IsSupported() { return g_supported; }

    void InstallHooks()
    {
        g_device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;
        if (!g_device) {
            logger::error("No D3D11 device, Reflex disabled");
            return;
        }

        g_supported = DetectNvidia(g_device);
        if (!g_supported) {
            logger::info("No NVIDIA GPU detected, Reflex disabled");
            return;
        }

        if (NvAPI_Initialize() != NVAPI_OK) {
            logger::warn("NvAPI_Initialize failed, Reflex disabled");
            g_supported = false;
            return;
        }

        if (!PresentHook::Install()) {
            logger::error("Reflex disabled");
            g_supported = false;
            return;
        }
        PresentHook::RegisterPrePresent(OnPrePresent);
        PresentHook::RegisterPostPresent(OnPostPresent);

        const auto target = RELOCATION_ID(35565, 36564).address() + REL::Relocate(0x1E, 0x3E, 0x33);
        Main_Update_Start::func = SKSE::GetTrampoline().write_call<5>(target, Main_Update_Start::thunk);

        RegisterPublishCallback(&OnPublish);
        ApplySleepMode(*ActiveSettings());

        logger::info("Reflex hooks installed");
    }
}
