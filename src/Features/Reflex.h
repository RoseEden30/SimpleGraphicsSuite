#pragma once

// NVIDIA Reflex low-latency mode, ported from doodlum's skyrim-nvidia-reflex.
// Device and swapchain come straight from RE::BSGraphics::Renderer instead of
// hooking D3D11CreateDeviceAndSwapChain, so no Detours dependency is needed -
// same vtable-patch technique as the rest of this plugin.
namespace Reflex
{
    void InstallHooks();

    // False on anything that isn't an NVIDIA GPU. The menu page uses this to
    // grey itself out instead of offering settings that can't do anything.
    bool IsSupported();
}
