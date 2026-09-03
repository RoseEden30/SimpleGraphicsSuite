#pragma once

// Scales the engine's own shadow softness with scene lighting - softer in
// dim/overcast light, sharper in direct sun, fixed indoors.
namespace SoftShadows
{
    void InstallHooks();
}
