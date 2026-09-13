#pragma once

// Single shared hook on IDXGISwapChain::Present. Reflex, post-processing and
// accessibility all need a per-frame callback; a vtable slot can only be
// patched once, so this owns it and dispatches to whoever registered.
namespace PresentHook
{
    using Callback = void (*)(
        REX::W32::ID3D11Device*, REX::W32::ID3D11DeviceContext*, REX::W32::IDXGISwapChain*);

    void RegisterPrePresent(Callback a_callback);
    void RegisterPostPresent(Callback a_callback);

    // Called once from plugin.cpp, after every module has registered. Returns
    // false if the device/swapchain aren't ready or the patch failed.
    bool Install();
}
