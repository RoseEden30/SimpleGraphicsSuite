#pragma once

// Intercepts D3D11CreateDeviceAndSwapChain through an IAT patch on the game's
// d3d11.dll import, so Streamline sees the device the moment it is created.
// Must run from SKSEPluginLoad, before the game gets there.
namespace DeviceHook
{
    void Install();
}
