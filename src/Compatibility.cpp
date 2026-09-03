#include "Compatibility.h"

#include "Config.h"

#include <psapi.h>

namespace Compatibility
{
    namespace
    {
        struct Entry
        {
            const wchar_t* dll;
            const char*    name;
            std::uint32_t  modules;
        };

        // Only place to edit to add a mod. Open Shaders ships the same DLL
        // name and takes over the same pipeline.
        constexpr Entry kKnown[] = {
            { L"CommunityShaders.dll", "Community Shaders", kAntiAliasing | kPostProcessing | kReflex | kSoftShadows },
            // Hooks the same TESCamera::Update call site to do the same job.
            { L"FirstPersonFOV.dll", "First Person FOV and Tween Menu Fix", kFieldOfView },
        };

        // ENB replaces d3d11.dll itself, so it can't be named like the mods above.
        bool IsEnbLoaded()
        {
            HMODULE modules[1024];
            DWORD   needed = 0;
            if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
                return false;

            const auto count = std::min<DWORD>(needed / sizeof(HMODULE), static_cast<DWORD>(std::size(modules)));
            for (DWORD i = 0; i < count; ++i) {
                if (GetProcAddress(modules[i], "ENBGetSDKVersion"))
                    return true;
            }
            return false;
        }

        struct Detection
        {
            std::uint32_t modules = 0;
            std::string   names;
        };

        // First use is past plugin loading, and nothing changes after that.
        const Detection& Detected()
        {
            static const Detection detection = [] {
                Detection result;
                if (IsIgnoringModConflicts())
                    return result;

                for (const auto& entry : kKnown) {
                    if (!GetModuleHandleW(entry.dll))
                        continue;

                    result.modules |= entry.modules;
                    if (!result.names.empty())
                        result.names += ", ";
                    result.names += entry.name;
                }

                if (IsEnbLoaded()) {
                    result.modules |= kAntiAliasing | kPostProcessing | kReflex | kSoftShadows;
                    if (!result.names.empty())
                        result.names += ", ";
                    result.names += "ENB";
                }

                if (result.modules)
                    logger::info("{} loaded - leaving the overlapping modules off", result.names);

                return result;
            }();

            return detection;
        }
    }

    bool IsSuppressed(Module a_module) { return (Detected().modules & a_module) != 0; }

    std::string_view DetectedNames() { return Detected().names; }
}
