#pragma once

#include <d3d11.h>
#include <sl.h>
#include <sl_dlss.h>

// NVIDIA Streamline interposer, resolved through GetProcAddress.
namespace Streamline
{
    // Loads sl.interposer.dll and calls slInit, once.
    void EnsureInitialized();

    // For adapters that can't run DLSS.
    void Skip();

    bool IsAvailable();

    // Per-adapter DLSS capability, set by SetDevice.
    bool IsDLSSSupported();

    // Swaps a D3D11/DXGI interface for Streamline's proxy, right after creation.
    sl::Result UpgradeInterface(void** a_interface);

    // Must be called once, immediately after the D3D11 device is created.
    void SetDevice(ID3D11Device* a_device);

    sl::Result GetNewFrameToken(sl::FrameToken*& a_token);
    sl::Result SetConstants(const sl::Constants& a_values, const sl::FrameToken& a_frame, const sl::ViewportHandle& a_viewport);
    sl::Result SetTag(
        const sl::ViewportHandle& a_viewport, const sl::ResourceTag* a_tags, std::uint32_t a_count, void* a_context);
    sl::Result EvaluateFeature(sl::Feature a_feature, const sl::FrameToken& a_frame, const sl::BaseStructure** a_inputs,
        std::uint32_t a_count, void* a_context);

    sl::Result DLSSSetOptions(const sl::ViewportHandle& a_viewport, const sl::DLSSOptions& a_options);
}
