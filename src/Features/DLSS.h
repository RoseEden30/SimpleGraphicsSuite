#pragma once

// NVIDIA DLAA - DLSS's native-resolution AA mode. The upscaling quality modes
// reconstructed only a fraction of the screen and were dropped rather than
// shipped broken; DLAA never upscales, so it is unaffected.
namespace DLSS
{
    // Idempotent - initializes NGX against the live D3D11 device and checks
    // Super Sampling support. Safe to call repeatedly.
    void EnsureInitialized();

    bool IsSupported();

    // Records the render resolution Apply and RecommendedMipBias work from.
    // Called every frame from AntiAliasing's Main_UpdateViewport hook while
    // DLAA is the active method.
    void UpdateJitter(RE::BSGraphics::State* a_state);

    // Writes this frame's sub-pixel jitter into the projection matrix. The
    // engine recomputes its own at a later call site, so this has to run from
    // that same one (AntiAliasing's Main_UpdateJitter) or it gets overwritten.
    void ApplyProjectionJitter(RE::BSGraphics::State* a_state);

    // Mip LOD bias recommended by NVIDIA's programming guide (section 3.5)
    // for the current render/display resolution ratio. 0 until UpdateJitter
    // has run at least once.
    float RecommendedMipBias();

    // Evaluates DLSS and copies the result back into a_colorResource, so it
    // has to run before the rest of the chain (bloom, SAO, tonemap) reads it.
    // Returns false and leaves a_colorResource untouched on any failure.
    bool Apply(ID3D11Resource* a_colorResource, std::uint32_t a_outWidth, std::uint32_t a_outHeight);

    // Logged once per distinct reason, so a failure that repeats every frame
    // doesn't flood the log.
    void SetLastFailureReason(std::string a_reason);
}
