#pragma once

// Optional: if NativeSystemMenuFramework is installed, adds real vanilla-widget
// settings to Skyrim's own System menu - the Display tab, plus new
// Accessibility/Performance/Effects/LUT tabs and a Simple Graphics Suite
// tab. No-ops entirely if NativeSystemMenuFramework isn't present.
namespace NativeMenuIntegration
{
    void Register();
}
