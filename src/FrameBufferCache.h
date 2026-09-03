#pragma once

#include <SimpleMath.h>

// Mirrors the engine's own per-frame camera constant buffer (the same one
// its shaders read at register b12) on the CPU side, so hooks that need
// real view/projection matrices - like feeding DLSS through Streamline -
// don't have to reconstruct them by hand.
namespace FrameBufferCache
{
    using Matrix = DirectX::SimpleMath::Matrix;
    using Vector4 = DirectX::SimpleMath::Vector4;

    struct Data
    {
        Matrix  cameraView;
        Matrix  cameraProj;
        Matrix  cameraViewProj;
        Matrix  cameraViewProjUnjittered;
        Matrix  cameraPreviousViewProjUnjittered;
        Matrix  cameraProjUnjittered;
        Matrix  cameraProjUnjitteredInverse;
        Matrix  cameraViewInverse;
        Matrix  cameraViewProjInverse;
        Matrix  cameraProjInverse;
        Vector4 cameraPosAdjust;
        Vector4 cameraPreviousPosAdjust;
        Vector4 frameParams;
        Vector4 dynamicResolutionParams1;
        Vector4 dynamicResolutionParams2;
    };

    // Only worth installing when something reads Get() - the hooks sit on
    // every Map/Unmap the engine makes.
    void InstallHooks();

    // Empty (all-zero) until the engine has mapped/unmapped its per-frame
    // buffer at least once - true from partway through the first frame on.
    const Data& Get();
}
