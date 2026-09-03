#pragma once

// Single shared hook on IDXGISwapChain::Present. Reflex and Post-processing
// both need a callback every frame; a vtable slot can only be patched once,
// so this is the one place that owns it and dispatches to whoever registered.
namespace PresentHook
{
    using Callback = void (*)(
        REX::W32::ID3D11Device*, REX::W32::ID3D11DeviceContext*, REX::W32::IDXGISwapChain*);

    // Safe to call more than once - only the first call actually patches the
    // vtable slot. Returns false if the device/swapchain aren't ready yet or
    // the patch failed.
    bool Install();

    void RegisterPrePresent(Callback a_callback);
    void RegisterPostPresent(Callback a_callback);
}
