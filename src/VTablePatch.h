#pragma once

// Swaps one vtable slot on a live C++ object for a hook function, the way
// PresentHook patches IDXGISwapChain::Present and PostProcessing patches
// BSShader::SetupTechnique. Thread-safe swap via InterlockedExchangePointer;
// VirtualProtect is needed since engine vtables are read-only pages.
namespace VTablePatch
{
    inline bool PatchSlot(void* a_object, std::size_t a_slot, void* a_hook, void** a_original)
    {
        auto** vtable = *reinterpret_cast<void***>(a_object);
        void** entry = &vtable[a_slot];

        DWORD previousProtection = 0;
        if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &previousProtection))
            return false;

        void* previous = *entry;
        *a_original = previous;

        void* swapped = InterlockedExchangePointer(static_cast<void* volatile*>(entry), a_hook);

        const bool installed = swapped == previous && previous != nullptr;
        if (!installed) {
            InterlockedExchangePointer(static_cast<void* volatile*>(entry), swapped);
            *a_original = nullptr;
        }

        VirtualProtect(entry, sizeof(void*), previousProtection, &previousProtection);
        return installed;
    }
}
