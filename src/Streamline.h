#pragma once

#include <d3d11.h>
#include <sl.h>
#include <sl_dlss.h>

// Thin wrapper around NVIDIA's Streamline interposer, resolved through
// GetProcAddress so nothing links against it. DLSS goes through Streamline
// rather than raw NGX for its camera-aware reprojection and its reactive /
// transparency mask support.
namespace Streamline
{
    // Idempotent - loads sl.interposer.dll and calls slInit. Safe to call
    // repeatedly; does nothing once already attempted.
    void EnsureInitialized();

    bool IsAvailable();

    // Replaces a raw D3D11/DXGI interface pointer with Streamline's proxy in
    // place. Manual-hooking mode needs it to observe Present. Call it right
    // after the interface is created, before anything else uses it.
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
