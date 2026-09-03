#pragma once

// Colorblind filter (Daltonize), applied over the fully composited back
// buffer right before Present - corrects the game's UI too, not just the
// 3D scene, and stays independent of every other effect.
namespace Accessibility
{
    void InstallHooks();
}
