#pragma once

// Dev-only diagnostics, gated behind [Debug] Enabled=1 in the ini (see
// Config.h - read once at startup, not a normal live setting).
namespace Debug
{
    // "NVIDIA GeForce RTX 4080 (NVIDIA)" - the adapter the game actually
    // renders on, which is not always the Windows default.
    std::string GpuSummary();
}
