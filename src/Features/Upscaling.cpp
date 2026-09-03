#include "Upscaling.h"

#include "Config.h"

namespace Upscaling
{
    namespace
    {
        // The engine keeps this ratio between frames, so it has to be put
        // back explicitly once the override stops.
        bool g_overriding = false;
    }

    bool IsActive(const Settings& a_settings)
    {
        if (!a_settings.masterEnabled || !a_settings.upscaling.enabled)
            return false;

        const auto& aa = a_settings.antiAliasing;
        return !(aa.enabled && (aa.method == 1 || aa.method == 2));
    }

    void Update(RE::BSGraphics::State& a_state)
    {
        const auto settings = ActiveSettings();

        auto& runtimeData = a_state.GetRuntimeData();

        if (!IsActive(*settings)) {
            if (g_overriding) {
                runtimeData.dynamicResolutionWidthRatio = 1.0f;
                runtimeData.dynamicResolutionHeightRatio = 1.0f;
                g_overriding = false;
            }
            return;
        }

        const float scale = settings->upscaling.renderScale;
        runtimeData.dynamicResolutionWidthRatio = scale;
        runtimeData.dynamicResolutionHeightRatio = scale;
        g_overriding = true;
    }
}
