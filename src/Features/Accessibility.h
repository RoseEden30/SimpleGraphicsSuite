#pragma once

// Colorblind filter (Daltonize) and high contrast edge boost, applied over
// the fully composited back buffer right before Present - corrects the
// game's UI too, not just the 3D scene, and stays independent of every
// other effect.
namespace Accessibility
{
    void InstallHooks();
}
