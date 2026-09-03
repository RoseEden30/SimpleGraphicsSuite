#pragma once

namespace Logging
{
    // Sets up the default spdlog logger, writing to the SKSE log folder under
    // Documents. Call this first thing in SKSEPluginLoad.
    void Init();

    // Switches between debug and info level. Callable any time.
    void SetVerbose(bool a_verbose = true);
}
