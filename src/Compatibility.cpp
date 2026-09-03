#include "Compatibility.h"

#include "Config.h"

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
        };

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
